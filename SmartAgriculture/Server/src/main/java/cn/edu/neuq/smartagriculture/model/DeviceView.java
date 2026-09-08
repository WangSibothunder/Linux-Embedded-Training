package cn.edu.neuq.smartagriculture.model;

public class DeviceView {
    private final String deviceId;
    private final String clientId;
    private final boolean online;
    private final Long connectedAt;
    private final Long disconnectedAt;
    private final long lastSeen;
    private final Long lastReportAt;
    private final Telemetry latest;

    public DeviceView(String deviceId, String clientId, boolean online, Long connectedAt,
                      Long disconnectedAt, long lastSeen, Long lastReportAt, Telemetry latest) {
        this.deviceId = deviceId;
        this.clientId = clientId;
        this.online = online;
        this.connectedAt = connectedAt;
        this.disconnectedAt = disconnectedAt;
        this.lastSeen = lastSeen;
        this.lastReportAt = lastReportAt;
        this.latest = latest;
    }

    public String getDeviceId() { return deviceId; }
    public String getClientId() { return clientId; }
    public boolean isOnline() { return online; }
    public Long getConnectedAt() { return connectedAt; }
    public Long getDisconnectedAt() { return disconnectedAt; }
    public long getLastSeen() { return lastSeen; }
    public Long getLastReportAt() { return lastReportAt; }
    public Telemetry getLatest() { return latest; }
}
