package cn.edu.neuq.smartagriculture.service;

import cn.edu.neuq.smartagriculture.config.MqttProperties;
import cn.edu.neuq.smartagriculture.model.DeviceView;
import cn.edu.neuq.smartagriculture.model.Telemetry;
import org.springframework.stereotype.Service;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Pattern;

@Service
public class DeviceStore {
    private static final Pattern DEVICE_ID = Pattern.compile("[A-Za-z0-9_-]{1,64}");
    private final Map<String, DeviceRecord> devices = new ConcurrentHashMap<>();
    private final int maxDevices;
    private final int historyLimit;

    public DeviceStore(MqttProperties properties) {
        this.maxDevices = Math.max(1, properties.getDevices());
        this.historyLimit = Math.max(1, properties.getHistoryLimit());
    }

    public boolean validDeviceId(String value) {
        return value != null && DEVICE_ID.matcher(value).matches();
    }

    public DeviceView connected(String clientId) {
        requireDeviceId(clientId);
        DeviceRecord record = getOrCreate(clientId);
        synchronized (record) {
            long now = System.currentTimeMillis();
            record.clientId = clientId;
            record.online = true;
            record.connectedAt = now;
            record.disconnectedAt = null;
            record.lastSeen = now;
            return record.view();
        }
    }

    public DeviceView disconnected(String clientId) {
        DeviceRecord record = devices.get(clientId);
        if (record == null) return null;
        synchronized (record) {
            long now = System.currentTimeMillis();
            record.online = false;
            record.disconnectedAt = now;
            record.lastSeen = now;
            return record.view();
        }
    }

    public DeviceView telemetry(String deviceId, String clientId, Telemetry telemetry) {
        requireDeviceId(deviceId);
        DeviceRecord record = getOrCreate(deviceId);
        synchronized (record) {
            long now = System.currentTimeMillis();
            record.clientId = clientId;
            record.online = true;
            record.lastSeen = now;
            record.lastReportAt = now;
            // Use the server clock so history remains trustworthy even when an
            // embedded board has no RTC or has not completed time sync yet.
            telemetry.setTimestamp(now);
            record.latest = telemetry;
            record.history.addLast(telemetry);
            while (record.history.size() > historyLimit) record.history.removeFirst();
            return record.view();
        }
    }

    public List<DeviceView> all() {
        List<DeviceView> result = new ArrayList<>();
        for (DeviceRecord record : devices.values()) {
            synchronized (record) { result.add(record.view()); }
        }
        result.sort(Comparator.comparing(DeviceView::getDeviceId));
        return result;
    }

    public DeviceView find(String deviceId) {
        DeviceRecord record = devices.get(deviceId);
        if (record == null) return null;
        synchronized (record) { return record.view(); }
    }

    public List<Telemetry> history(String deviceId, int limit) {
        DeviceRecord record = devices.get(deviceId);
        if (record == null) return null;
        synchronized (record) {
            int count = Math.min(Math.max(1, limit), historyLimit);
            List<Telemetry> values = new ArrayList<>(record.history);
            int from = Math.max(0, values.size() - count);
            return new ArrayList<>(values.subList(from, values.size()));
        }
    }

    private DeviceRecord getOrCreate(String id) {
        DeviceRecord existing = devices.get(id);
        if (existing != null) return existing;
        synchronized (devices) {
            existing = devices.get(id);
            if (existing != null) return existing;
            if (devices.size() >= maxDevices) throw new IllegalStateException("Device limit reached");
            DeviceRecord created = new DeviceRecord(id);
            devices.put(id, created);
            return created;
        }
    }

    private void requireDeviceId(String id) {
        if (!validDeviceId(id)) throw new IllegalArgumentException("Invalid device ID: " + id);
    }

    private static class DeviceRecord {
        final String deviceId;
        String clientId;
        boolean online;
        Long connectedAt;
        Long disconnectedAt;
        long lastSeen;
        Long lastReportAt;
        Telemetry latest;
        final Deque<Telemetry> history = new ArrayDeque<>();

        DeviceRecord(String deviceId) { this.deviceId = deviceId; }
        DeviceView view() {
            return new DeviceView(deviceId, clientId, online, connectedAt, disconnectedAt,
                    lastSeen, lastReportAt, latest);
        }
    }
}
