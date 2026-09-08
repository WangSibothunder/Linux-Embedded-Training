package cn.edu.neuq.smartagriculture.service;

import cn.edu.neuq.smartagriculture.config.MqttProperties;
import cn.edu.neuq.smartagriculture.model.DeviceView;
import cn.edu.neuq.smartagriculture.model.Telemetry;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.moquette.broker.Server;
import io.moquette.broker.config.IConfig;
import io.moquette.broker.config.MemoryConfig;
import io.moquette.interception.AbstractInterceptHandler;
import io.moquette.interception.InterceptHandler;
import io.moquette.interception.messages.InterceptConnectMessage;
import io.moquette.interception.messages.InterceptConnectionLostMessage;
import io.moquette.interception.messages.InterceptDisconnectMessage;
import io.moquette.interception.messages.InterceptPublishMessage;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.handler.codec.mqtt.MqttMessageBuilders;
import io.netty.handler.codec.mqtt.MqttPublishMessage;
import io.netty.handler.codec.mqtt.MqttQoS;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.Collections;
import java.util.Properties;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

@Service
public class MqttBrokerService {
    private static final Logger log = LoggerFactory.getLogger(MqttBrokerService.class);
    private final MqttProperties properties;
    private final DeviceStore store;
    private final TelemetryValidator validator;
    private final SseHub events;
    private final ObjectMapper mapper;
    private final Server broker = new Server();
    private Pattern telemetryTopic;

    public MqttBrokerService(MqttProperties properties, DeviceStore store,
                             TelemetryValidator validator, SseHub events, ObjectMapper mapper) {
        this.properties = properties;
        this.store = store;
        this.validator = validator;
        this.events = events;
        this.mapper = mapper;
    }

    @PostConstruct
    public void start() throws Exception {
        if (!properties.isEnabled()) return;
        Path dataPath = Paths.get(properties.getDataPath()).toAbsolutePath().normalize();
        Files.createDirectories(dataPath);
        telemetryTopic = Pattern.compile("^" + Pattern.quote(properties.getTelemetryTopicPrefix()) +
                "/([A-Za-z0-9_-]{1,64})/telemetry$");
        Properties config = new Properties();
        config.setProperty(IConfig.HOST_PROPERTY_NAME, properties.getHost());
        config.setProperty(IConfig.PORT_PROPERTY_NAME, Integer.toString(properties.getPort()));
        config.setProperty(IConfig.ALLOW_ANONYMOUS_PROPERTY_NAME,
                Boolean.toString(properties.isAllowAnonymous()));
        config.setProperty(IConfig.DATA_PATH_PROPERTY_NAME, dataPath.toString());
        config.setProperty(IConfig.PERSISTENCE_ENABLED_PROPERTY_NAME, "false");
        config.setProperty(IConfig.ENABLE_TELEMETRY_NAME, "false");
        config.setProperty(IConfig.NETTY_MAX_BYTES_PROPERTY_NAME,
                Integer.toString(properties.getMaxPayloadBytes()));
        InterceptHandler handler = new Handler();
        broker.startServer(new MemoryConfig(config), Collections.singletonList(handler));
        log.info("MQTT broker listening on {}:{}", properties.getHost(), properties.getPort());
    }

    public void publishCommand(String deviceId, byte[] payload) {
        String topic = properties.getTelemetryTopicPrefix() + "/" + deviceId + "/commands";
        MqttPublishMessage message = MqttMessageBuilders.publish()
                .topicName(topic)
                .retained(false)
                .qos(MqttQoS.AT_LEAST_ONCE)
                .payload(Unpooled.wrappedBuffer(payload))
                .build();
        broker.internalPublish(message, "smart-agriculture-server");
    }

    @PreDestroy
    public void stop() {
        if (properties.isEnabled()) broker.stopServer();
    }

    private class Handler extends AbstractInterceptHandler {
        @Override
        public String getID() { return "smart-agriculture-events"; }

        @Override
        public void onSessionLoopError(Throwable error) {
            log.warn("MQTT session loop error: {}", error.getMessage());
        }

        @Override
        public void onConnect(InterceptConnectMessage message) {
            try {
                events.status(store.connected(message.getClientID()));
            } catch (RuntimeException error) {
                log.warn("Ignoring MQTT CONNECT from {}: {}", message.getClientID(), error.getMessage());
            }
        }

        @Override
        public void onDisconnect(InterceptDisconnectMessage message) {
            events.status(store.disconnected(message.getClientID()));
        }

        @Override
        public void onConnectionLost(InterceptConnectionLostMessage message) {
            events.status(store.disconnected(message.getClientID()));
        }

        @Override
        public void onPublish(InterceptPublishMessage message) {
            ByteBuf buffer = message.getPayload();
            try {
                Matcher match = telemetryTopic.matcher(message.getTopicName());
                if (!match.matches()) return;
                String deviceId = match.group(1);
                if (buffer.readableBytes() > properties.getMaxPayloadBytes()) {
                    log.warn("Dropped oversized telemetry from {}", deviceId);
                    return;
                }
                byte[] payload = new byte[buffer.readableBytes()];
                buffer.getBytes(buffer.readerIndex(), payload);
                Telemetry telemetry = mapper.readValue(payload, Telemetry.class);
                validator.validate(telemetry);
                DeviceView status = store.telemetry(deviceId, message.getClientID(), telemetry);
                events.telemetry(deviceId, telemetry);
                events.status(status);
            } catch (Exception error) {
                log.warn("Dropped invalid telemetry on {}: {}", message.getTopicName(), error.getMessage());
            } finally {
                buffer.release();
            }
        }
    }
}
