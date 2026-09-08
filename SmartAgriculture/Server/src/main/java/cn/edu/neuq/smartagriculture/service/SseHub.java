package cn.edu.neuq.smartagriculture.service;

import cn.edu.neuq.smartagriculture.config.MqttProperties;
import cn.edu.neuq.smartagriculture.model.DeviceView;
import cn.edu.neuq.smartagriculture.model.Telemetry;
import org.springframework.stereotype.Service;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import javax.annotation.PreDestroy;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

@Service
public class SseHub {
    private final List<Client> clients = new CopyOnWriteArrayList<>();
    private final ExecutorService sender;

    public SseHub(MqttProperties properties) {
        int capacity = Math.max(16, properties.getSseQueueCapacity());
        this.sender = new ThreadPoolExecutor(1, 2, 30, TimeUnit.SECONDS,
                new LinkedBlockingQueue<>(capacity), new ThreadPoolExecutor.DiscardOldestPolicy());
    }

    public SseEmitter subscribe(String deviceId) {
        SseEmitter emitter = new SseEmitter(0L);
        Client client = new Client(deviceId, emitter);
        clients.add(client);
        Runnable remove = () -> clients.remove(client);
        emitter.onCompletion(remove);
        emitter.onTimeout(remove);
        emitter.onError(error -> remove.run());
        try {
            emitter.send(SseEmitter.event().name("ready").data("connected"));
        } catch (IOException error) {
            clients.remove(client);
        }
        return emitter;
    }

    public void telemetry(String deviceId, Telemetry telemetry) {
        broadcast(deviceId, "telemetry", telemetry);
    }

    public void status(DeviceView status) {
        if (status != null) broadcast(status.getDeviceId(), "device-status", status);
    }

    private void broadcast(String deviceId, String eventName, Object data) {
        for (Client client : clients) {
            if (client.deviceId == null || client.deviceId.equals(deviceId)) {
                sender.execute(() -> send(client, deviceId, eventName, data));
            }
        }
    }

    private void send(Client client, String deviceId, String eventName, Object data) {
        try {
            client.emitter.send(SseEmitter.event().id(deviceId).name(eventName).data(data));
        } catch (Exception error) {
            clients.remove(client);
            client.emitter.complete();
        }
    }

    @PreDestroy
    public void stop() {
        for (Client client : clients) client.emitter.complete();
        sender.shutdownNow();
    }

    private static class Client {
        final String deviceId;
        final SseEmitter emitter;
        Client(String deviceId, SseEmitter emitter) {
            this.deviceId = deviceId;
            this.emitter = emitter;
        }
    }
}
