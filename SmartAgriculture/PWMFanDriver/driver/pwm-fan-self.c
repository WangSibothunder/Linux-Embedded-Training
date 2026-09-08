#include <linux/device.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define MAX_PWM 255
#define FAN_LEVEL_COUNT 5
#define MAX_TIMEOUT_SEC 86400

static const unsigned int pwm_fan_levels[FAN_LEVEL_COUNT] = {
	0, 64, 128, 192, 255
};

struct pwm_fan_self_dev {
	struct pwm_device *pwm;
	unsigned int value;
	bool enabled;
	unsigned int level;
	unsigned int timeout_sec;
	unsigned long timeout_expires;
	struct mutex lock;
	struct timer_list fan_timer;
	struct work_struct timeout_work;
};

static void pwm_fan_self_rearm_timer_locked(struct pwm_fan_self_dev *ctx)
{
	if (!ctx->timeout_sec || !ctx->value)
		return;

	ctx->timeout_expires = jiffies +
		msecs_to_jiffies(ctx->timeout_sec * 1000UL);
	mod_timer(&ctx->fan_timer, ctx->timeout_expires);
}

static void pwm_fan_self_timeout(unsigned long data)
{
	struct pwm_fan_self_dev *ctx = (struct pwm_fan_self_dev *)data;

	/* PWM operations may sleep, so never execute them in timer context. */
	schedule_work(&ctx->timeout_work);
}

static int pwm_fan_self_apply_locked(struct pwm_fan_self_dev *ctx,
					 unsigned int duty)
{
	unsigned long period;
	unsigned long pulse;
	int ret;

	if (duty > MAX_PWM)
		return -EINVAL;

	period = ctx->pwm->period;
	if (period == 0)
		return -EINVAL;

	pulse = DIV_ROUND_UP(duty * (period - 1), MAX_PWM);

	ret = pwm_config(ctx->pwm, pulse, period);
	if (ret)
		return ret;

	if (duty == 0) {
		pwm_disable(ctx->pwm);
		ctx->enabled = false;
	} else {
		if (!ctx->enabled) {
			ret = pwm_enable(ctx->pwm);
			if (ret)
				return ret;
			ctx->enabled = true;
		}
	}

	ctx->value = duty;
	return 0;
}

static int pwm_fan_self_set(struct pwm_fan_self_dev *ctx, unsigned int duty)
{
	int ret;

	mutex_lock(&ctx->lock);
	ret = pwm_fan_self_apply_locked(ctx, duty);
	if (!ret && duty)
		pwm_fan_self_rearm_timer_locked(ctx);
	mutex_unlock(&ctx->lock);

	if (!ret && !duty)
		del_timer_sync(&ctx->fan_timer);

	return ret;
}

static void pwm_fan_self_timeout_work(struct work_struct *work)
{
	struct pwm_fan_self_dev *ctx =
		container_of(work, struct pwm_fan_self_dev, timeout_work);

	mutex_lock(&ctx->lock);
	/* Ignore stale work if userspace rearmed or disabled the timeout. */
	if (ctx->timeout_sec && ctx->value &&
	    time_after_eq(jiffies, ctx->timeout_expires)) {
		if (!pwm_fan_self_apply_locked(ctx, 0)) {
			ctx->level = 0;
			ctx->timeout_sec = 0;
		}
	}
	mutex_unlock(&ctx->lock);
}

static ssize_t pwm_fan_self_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned int value;

	mutex_lock(&ctx->lock);
	value = ctx->value;
	mutex_unlock(&ctx->lock);
	return scnprintf(buf, PAGE_SIZE, "%u\n", value);
}

static ssize_t pwm_fan_self_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val > MAX_PWM)
		return -EINVAL;

	ret = pwm_fan_self_set(ctx, (unsigned int)val);
	if (ret)
		return ret;

	return count;
}

static ssize_t pwm_fan_self_level_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned int i;
	unsigned int value;
	unsigned int level;

	mutex_lock(&ctx->lock);
	value = ctx->value;
	level = ctx->level;
	mutex_unlock(&ctx->lock);

	for (i = 0; i < FAN_LEVEL_COUNT; i++) {
		if (value == pwm_fan_levels[i]) {
			return scnprintf(buf, PAGE_SIZE, "%u\n", i);
		}
	}

	return scnprintf(buf, PAGE_SIZE, "%u\n", level);
}

static ssize_t pwm_fan_self_level_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned long level;
	int ret;

	ret = kstrtoul(buf, 10, &level);
	if (ret)
		return ret;

	if (level >= FAN_LEVEL_COUNT)
		return -EINVAL;

	ret = pwm_fan_self_set(ctx, pwm_fan_levels[level]);
	if (ret)
		return ret;
	mutex_lock(&ctx->lock);
	ctx->level = level;
	mutex_unlock(&ctx->lock);

	return count;
}

static ssize_t pwm_fan_self_timeout_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned int timeout_sec;

	mutex_lock(&ctx->lock);
	timeout_sec = ctx->timeout_sec;
	mutex_unlock(&ctx->lock);
	return scnprintf(buf, PAGE_SIZE, "%u\n", timeout_sec);
}

static ssize_t pwm_fan_self_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct pwm_fan_self_dev *ctx = dev_get_drvdata(dev);
	unsigned long sec;
	int ret;

	ret = kstrtoul(buf, 10, &sec);
	if (ret)
		return ret;

	if (sec > MAX_TIMEOUT_SEC)
		return -ERANGE;

	mutex_lock(&ctx->lock);
	ctx->timeout_sec = (unsigned int)sec;
	if (ctx->timeout_sec && ctx->value)
		pwm_fan_self_rearm_timer_locked(ctx);
	mutex_unlock(&ctx->lock);

	if (!sec)
		del_timer_sync(&ctx->fan_timer);

	return count;
}

static DEVICE_ATTR(pwm, 0644, pwm_fan_self_show, pwm_fan_self_store);
static DEVICE_ATTR(fan_level, 0644,
			 pwm_fan_self_level_show, pwm_fan_self_level_store);
static DEVICE_ATTR(fan_timeout, 0644,
			 pwm_fan_self_timeout_show, pwm_fan_self_timeout_store);

static int pwm_fan_self_probe(struct platform_device *pdev)
{
	struct pwm_fan_self_dev *ctx;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->pwm = devm_of_pwm_get(&pdev->dev, pdev->dev.of_node, NULL);
	if (IS_ERR(ctx->pwm)) {
		dev_err(&pdev->dev, "failed to get pwm\n");
		return PTR_ERR(ctx->pwm);
	}

	ctx->value = MAX_PWM;
	ctx->enabled = false;
	ctx->level = FAN_LEVEL_COUNT - 1;
	ctx->timeout_sec = 0;
	ctx->timeout_expires = 0;
	mutex_init(&ctx->lock);
	setup_timer(&ctx->fan_timer, pwm_fan_self_timeout, (unsigned long)ctx);
	INIT_WORK(&ctx->timeout_work, pwm_fan_self_timeout_work);

	platform_set_drvdata(pdev, ctx);

	ret = pwm_fan_self_set(ctx, ctx->value);
	if (ret) {
		dev_err(&pdev->dev, "failed to set default pwm duty\n");
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_pwm);
	if (ret)
		goto err_pwm_attr;

	ret = device_create_file(&pdev->dev, &dev_attr_fan_level);
	if (ret)
		goto err_level_attr;

	ret = device_create_file(&pdev->dev, &dev_attr_fan_timeout);
	if (ret)
		goto err_timeout_attr;

	dev_info(&pdev->dev, "pwm-fan-self probe ok, pwm=%d\n", ctx->pwm->hwpwm);
	return 0;

err_timeout_attr:
	device_remove_file(&pdev->dev, &dev_attr_fan_level);
err_level_attr:
	device_remove_file(&pdev->dev, &dev_attr_pwm);
err_pwm_attr:
	if (ctx->enabled)
		pwm_disable(ctx->pwm);
	return ret;
}

static int pwm_fan_self_remove(struct platform_device *pdev)
{
	struct pwm_fan_self_dev *ctx = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_fan_timeout);
	device_remove_file(&pdev->dev, &dev_attr_fan_level);
	device_remove_file(&pdev->dev, &dev_attr_pwm);
	del_timer_sync(&ctx->fan_timer);
	cancel_work_sync(&ctx->timeout_work);
	mutex_lock(&ctx->lock);
	if (ctx->enabled)
		pwm_fan_self_apply_locked(ctx, 0);
	mutex_unlock(&ctx->lock);
	return 0;
}

static const struct of_device_id pwm_fan_self_match[] = {
	{ .compatible = "pwm-fan-self" },
	{ }
};

MODULE_DEVICE_TABLE(of, pwm_fan_self_match);

static struct platform_driver pwm_fan_self_driver = {
	.probe = pwm_fan_self_probe,
	.remove = pwm_fan_self_remove,
	.driver = {
		.name = "pwm-fan-self",
		.of_match_table = pwm_fan_self_match,
	},
};

module_platform_driver(pwm_fan_self_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("elf");
MODULE_DESCRIPTION("Simple PWM fan controller with level and timeout support");
MODULE_ALIAS("platform:pwm-fan-self");
