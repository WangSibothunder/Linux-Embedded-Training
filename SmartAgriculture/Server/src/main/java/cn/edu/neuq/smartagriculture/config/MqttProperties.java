package cn.edu.neuq.smartagriculture.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "mqtt")
public class MqttProperties {
    private boolean enabled = true;
    private String host = "0.0.0.0";
    private int port = 1883;
    private boolean allowAnonymous = true;
    private String dataPath = "./mqtt-data";
    private String telemetryTopicPrefix = "devices";
    private int devices = 40;
    private int maxPayloadBytes = 65536;
    private int historyLimit = 100;
    private int sseQueueCapacity = 256;

    public boolean isEnabled() { return enabled; }
    public void setEnabled(boolean enabled) { this.enabled = enabled; }
    public String getHost() { return host; }
    public void setHost(String host) { this.host = host; }
    public int getPort() { return port; }
    public void setPort(int port) { this.port = port; }
    public boolean isAllowAnonymous() { return allowAnonymous; }
    public void setAllowAnonymous(boolean allowAnonymous) { this.allowAnonymous = allowAnonymous; }
    public String getDataPath() { return dataPath; }
    public void setDataPath(String dataPath) { this.dataPath = dataPath; }
    public String getTelemetryTopicPrefix() { return telemetryTopicPrefix; }
    public void setTelemetryTopicPrefix(String telemetryTopicPrefix) { this.telemetryTopicPrefix = telemetryTopicPrefix; }
    public int getDevices() { return devices; }
    public void setDevices(int devices) { this.devices = devices; }
    public int getMaxPayloadBytes() { return maxPayloadBytes; }
    public void setMaxPayloadBytes(int maxPayloadBytes) { this.maxPayloadBytes = maxPayloadBytes; }
    public int getHistoryLimit() { return historyLimit; }
    public void setHistoryLimit(int historyLimit) { this.historyLimit = historyLimit; }
    public int getSseQueueCapacity() { return sseQueueCapacity; }
    public void setSseQueueCapacity(int sseQueueCapacity) { this.sseQueueCapacity = sseQueueCapacity; }
}
