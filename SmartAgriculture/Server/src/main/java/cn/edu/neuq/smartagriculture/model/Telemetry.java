package cn.edu.neuq.smartagriculture.model;

public class Telemetry {
    private Double temperature;
    private Double humidity;
    private Double light;
    private Integer curtainPosition;
    private Boolean lightOn;
    private Boolean fanOn;
    private Long timestamp;

    public Double getTemperature() { return temperature; }
    public void setTemperature(Double temperature) { this.temperature = temperature; }
    public Double getHumidity() { return humidity; }
    public void setHumidity(Double humidity) { this.humidity = humidity; }
    public Double getLight() { return light; }
    public void setLight(Double light) { this.light = light; }
    public Integer getCurtainPosition() { return curtainPosition; }
    public void setCurtainPosition(Integer curtainPosition) { this.curtainPosition = curtainPosition; }
    public Boolean getLightOn() { return lightOn; }
    public void setLightOn(Boolean lightOn) { this.lightOn = lightOn; }
    public Boolean getFanOn() { return fanOn; }
    public void setFanOn(Boolean fanOn) { this.fanOn = fanOn; }
    public Long getTimestamp() { return timestamp; }
    public void setTimestamp(Long timestamp) { this.timestamp = timestamp; }
}
