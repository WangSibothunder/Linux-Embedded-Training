package cn.edu.neuq.smartagriculture;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.EnableConfigurationProperties;

import cn.edu.neuq.smartagriculture.config.MqttProperties;

@SpringBootApplication
@EnableConfigurationProperties(MqttProperties.class)
public class SmartAgricultureServerApplication {
    public static void main(String[] args) {
        SpringApplication.run(SmartAgricultureServerApplication.class, args);
    }
}
