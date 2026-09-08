package cn.edu.neuq.smartagriculture.service;

import cn.edu.neuq.smartagriculture.model.Telemetry;
import org.springframework.stereotype.Component;

@Component
public class TelemetryValidator {
    public void validate(Telemetry value) {
        requireFinite(value.getTemperature(), "temperature");
        requireFinite(value.getHumidity(), "humidity");
        requireFinite(value.getLight(), "light");
        if (value.getHumidity() < 0 || value.getHumidity() > 100)
            throw new IllegalArgumentException("humidity must be between 0 and 100");
        if (value.getLight() < 0)
            throw new IllegalArgumentException("light must be >= 0");
        if (value.getCurtainPosition() != null &&
                (value.getCurtainPosition() < 0 || value.getCurtainPosition() > 100))
            throw new IllegalArgumentException("curtainPosition must be between 0 and 100");
        if (value.getLightOn() == null) throw new IllegalArgumentException("lightOn is required");
        if (value.getFanOn() == null) throw new IllegalArgumentException("fanOn is required");
    }

    private void requireFinite(Double value, String name) {
        if (value == null || value.isNaN() || value.isInfinite())
            throw new IllegalArgumentException(name + " must be a finite number");
    }
}
