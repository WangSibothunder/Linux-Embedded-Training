package cn.edu.neuq.smartagriculture.web;

import cn.edu.neuq.smartagriculture.model.DeviceView;
import cn.edu.neuq.smartagriculture.model.Telemetry;
import cn.edu.neuq.smartagriculture.service.DeviceStore;
import cn.edu.neuq.smartagriculture.service.MqttBrokerService;
import cn.edu.neuq.smartagriculture.service.SseHub;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import java.util.Collections;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api")
public class DeviceController {
    private final DeviceStore store;
    private final SseHub events;
    private final MqttBrokerService broker;
    private final ObjectMapper mapper;

    public DeviceController(DeviceStore store, SseHub events,
                            MqttBrokerService broker, ObjectMapper mapper) {
        this.store = store;
        this.events = events;
        this.broker = broker;
        this.mapper = mapper;
    }

    @GetMapping({"/devices", "/devices/status"})
    public List<DeviceView> devices() { return store.all(); }

    @GetMapping("/devices/{deviceId}")
    public DeviceView device(@PathVariable String deviceId) {
        validateId(deviceId);
        DeviceView result = store.find(deviceId);
        if (result == null) throw notFound(deviceId);
        return result;
    }

    @GetMapping("/devices/{deviceId}/telemetry/latest")
    public Telemetry latest(@PathVariable String deviceId) {
        DeviceView view = device(deviceId);
        if (view.getLatest() == null)
            throw new ApiException(HttpStatus.NOT_FOUND, "No telemetry for device: " + deviceId);
        return view.getLatest();
    }

    @GetMapping("/devices/{deviceId}/telemetry")
    public List<Telemetry> history(@PathVariable String deviceId,
                                   @RequestParam(defaultValue = "100") int limit) {
        validateId(deviceId);
        List<Telemetry> values = store.history(deviceId, limit);
        if (values == null) throw notFound(deviceId);
        return values;
    }

    @PostMapping(value = "/devices/{deviceId}/commands", consumes = MediaType.APPLICATION_JSON_VALUE)
    public Map<String, String> command(@PathVariable String deviceId, @RequestBody JsonNode body) {
        validateId(deviceId);
        if (store.find(deviceId) == null) throw notFound(deviceId);
        validateCommand(body);
        try {
            broker.publishCommand(deviceId, mapper.writeValueAsBytes(body));
        } catch (Exception error) {
            throw new ApiException(HttpStatus.INTERNAL_SERVER_ERROR, "Failed to publish command");
        }
        return Collections.singletonMap("status", "published");
    }

    @GetMapping(value = "/telemetry/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter allEvents() { return events.subscribe(null); }

    @GetMapping(value = "/devices/{deviceId}/telemetry/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter deviceEvents(@PathVariable String deviceId) {
        validateId(deviceId);
        if (store.find(deviceId) == null) throw notFound(deviceId);
        return events.subscribe(deviceId);
    }

    private void validateCommand(JsonNode body) {
        if (body == null || !body.isObject() || !body.has("type") || !body.get("type").isTextual()
                || !body.has("value") || body.size() != 2)
            throw invalidRequest();
        String type = body.get("type").asText();
        JsonNode value = body.get("value");
        if (("light".equals(type) || "fan".equals(type)) && value.isBoolean()) return;
        if ("curtain".equals(type) && value.isIntegralNumber()
                && value.asInt() >= 0 && value.asInt() <= 100) return;
        throw invalidRequest();
    }

    private void validateId(String id) {
        if (!store.validDeviceId(id))
            throw new ApiException(HttpStatus.BAD_REQUEST, "Invalid device ID: " + id);
    }

    private ApiException notFound(String id) {
        return new ApiException(HttpStatus.NOT_FOUND, "Device not found: " + id);
    }

    private ApiException invalidRequest() {
        return new ApiException(HttpStatus.BAD_REQUEST, "Invalid request");
    }
}
