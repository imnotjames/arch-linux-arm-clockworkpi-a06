// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Clockwork Tech LLC
 * Copyright (c) 2021-2022 Max Fierke <max@maxfierke.com>
 *
 * Based on Pinfan Zhu's work on panel-cwd686.c for ClockworkPi's 5.10 BSP
 */

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/drm_modes.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

struct cwd686 {
	struct device *dev;
	struct drm_panel panel;
	struct regulator *vci;
	struct regulator *iovcc;
	struct gpio_desc *reset_gpio;
	enum drm_panel_orientation orientation;
	bool prepared;
};

#define CWD686_HPX 480 /* Horizontal in pixels */
#define CWD686_HFP 150 /* HFP = 150 */
#define CWD686_HSW 24 /* HSW = 24 */
#define CWD686_HBP 40 /* HBP = 40 */

#define CWD686_VPX 1280 /* Vertical in pixels */
#define CWD686_VFP 12 /* VFP = 12 */
#define CWD686_VSW 6 /* VSW = 6 */
#define CWD686_VBP 10 /* VBP = 10 */

#define CWD686_FPS 60

static const struct drm_display_mode default_mode = {
	.clock = (
		(CWD686_HPX + CWD686_HFP + CWD686_HBP + CWD686_HSW) *
		(CWD686_VPX + CWD686_VFP + CWD686_VBP + CWD686_VSW) *
		CWD686_FPS / 1000
	),
	.hdisplay =    CWD686_HPX,
	.hsync_start = CWD686_HPX + CWD686_HFP,
	.hsync_end =   CWD686_HPX + CWD686_HFP + CWD686_HSW,
	.htotal =      CWD686_HPX + CWD686_HFP + CWD686_HSW + CWD686_HBP,
	.vdisplay =    CWD686_VPX,
	.vsync_start = CWD686_VPX + CWD686_VFP,
	.vsync_end =   CWD686_VPX + CWD686_VFP + CWD686_VSW,
	.vtotal =      CWD686_VPX + CWD686_VFP + CWD686_VSW + CWD686_VBP,
};

static inline struct cwd686 *panel_to_cwd686(struct drm_panel *panel)
{
	return container_of(panel, struct cwd686, panel);
}

#define ICNL9707_CMD_NOP 0x00
#define ICNL9707_CMD_SWRESET 0x01
#define ICNL9707_CMD_RDID1 0xDA
#define ICNL9707_CMD_RDID2 0xDB
#define ICNL9707_CMD_RDID3 0xDC


#define ICNL9707_CMD_CGOUTL 0xB3
#define ICNL9707_CMD_CGOUTR 0xB4

#define ICNL9707_CMD_UNLOCK_REGISTER 0xF0

#define ICNL9707_CMD_PWRCON_VCOM 0xB6
#define ICNL9707_CMD_PWRCON_SEQ 0xB7
#define ICNL9707_CMD_PWRCON_CLK 0xB8
#define ICNL9707_CMD_PWRCON_BTA 0xB9
#define ICNL9707_CMD_PWRCON_MODE 0xBA
#define ICNL9707_CMD_PWRCON_REG 0xBD
#define ICNL9707_CMD_TCON 0xC1
#define ICNL9707_CMD_TCON2 0xC2
#define ICNL9707_CMD_TCON3 0xC3
#define ICNL9707_CMD_SRC_TIM 0xC6
#define ICNL9707_CMD_SRCCON 0xC7
#define ICNL9707_CMD_SET_GAMMA 0xC8
#define ICNL9707_CMD_ETC 0xD0

#define ICNL9707_P_PWRCON_VCOM_0495V 0x0D

#define ICNL9707_P_CGOUT_VGL 0x00
#define ICNL9707_P_CGOUT_VGH 0x01
#define ICNL9707_P_CGOUT_HZ 0x02
#define ICNL9707_P_CGOUT_GND 0x03
#define ICNL9707_P_CGOUT_GSP1 0x04
#define ICNL9707_P_CGOUT_GSP2 0x05
#define ICNL9707_P_CGOUT_GSP3 0x06
#define ICNL9707_P_CGOUT_GSP4 0x07
#define ICNL9707_P_CGOUT_GSP5 0x08
#define ICNL9707_P_CGOUT_GSP6 0x09
#define ICNL9707_P_CGOUT_GSP7 0x0A
#define ICNL9707_P_CGOUT_GSP8 0x0B
#define ICNL9707_P_CGOUT_GCK1 0x0C
#define ICNL9707_P_CGOUT_GCK2 0x0D
#define ICNL9707_P_CGOUT_GCK3 0x0E
#define ICNL9707_P_CGOUT_GCK4 0x0F
#define ICNL9707_P_CGOUT_GCK5 0x10
#define ICNL9707_P_CGOUT_GCK6 0x11
#define ICNL9707_P_CGOUT_GCK7 0x12
#define ICNL9707_P_CGOUT_GCK8 0x13
#define ICNL9707_P_CGOUT_GCK9 0x14
#define ICNL9707_P_CGOUT_GCK10 0x15
#define ICNL9707_P_CGOUT_GCK11 0x16
#define ICNL9707_P_CGOUT_GCK12 0x17
#define ICNL9707_P_CGOUT_GCK13 0x18
#define ICNL9707_P_CGOUT_GCK14 0x19
#define ICNL9707_P_CGOUT_GCK15 0x1A
#define ICNL9707_P_CGOUT_GCK16 0x1B
#define ICNL9707_P_CGOUT_DIR 0x1C
#define ICNL9707_P_CGOUT_DIRB 0x1D
#define ICNL9707_P_CGOUT_ECLK_AC 0x1E
#define ICNL9707_P_CGOUT_ECLK_ACB 0x1F
#define ICNL9707_P_CGOUT_ECLK_AC2 0x20
#define ICNL9707_P_CGOUT_ECLK_AC2B 0x21
#define ICNL9707_P_CGOUT_GCH 0x22
#define ICNL9707_P_CGOUT_GCL 0x23
#define ICNL9707_P_CGOUT_XDON 0x24
#define ICNL9707_P_CGOUT_XDONB 0x25

#define ICNL9707_TCON2_720RGB 0x00
#define ICNL9707_TCON2_600RGB 0x01
#define ICNL9707_TCON2_640RGB 0x03

#define ICNL9707_TCON3_REV_EOR 0x40
#define ICNL9707_TCON3_B4_EOR 0x30
#define ICNL9707_TCON3_B3_EOR 0x20
#define ICNL9707_TCON3_B2_EOR 0x10

#define ICNL9707_SRCCON_ZSHIFT_ENABLE 0x48
#define ICNL9707_SRCCON_ZSHIFT_DISABLE 0x41
#define ICNL9707_SRCCON_ZLINE_ENABLE 0x44
#define ICNL9707_SRCCON_ZLINE_DISABLE 0x41

#define ICNL9707_MADCTL_ML  0x10
#define ICNL9707_MADCTL_RGB 0x00
#define ICNL9707_MADCTL_BGR 0x08
#define ICNL9707_MADCTL_MH  0x04

static int cwd686_ids_show(struct cwd686 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret = 0;

	u8 id1, id2, id3;

	ret = mipi_dsi_dcs_read(dsi, ICNL9707_CMD_RDID1, &id1, 1);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to RDID1 (%d)\n", ret);
	}
	ret = mipi_dsi_dcs_read(dsi, ICNL9707_CMD_RDID2, &id2, 1);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to RDID2 (%d)\n", ret);
	}
	ret = mipi_dsi_dcs_read(dsi, ICNL9707_CMD_RDID3, &id3, 1);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to RDID3 (%d)\n", ret);
	}

	dev_warn(ctx->dev, "ID1:%04X ID2:%04X ID3:%04X\n", id1, id2, id3);

	return ret;
}

static int cwd686_init_sequence(struct cwd686 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	/* Enable access to Level 2 registers */
	/*mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_PASSWORD1,
		    ICNL9707_P_PASSWORD1_ENABLE_LVL2,
		    ICNL9707_P_PASSWORD1_ENABLE_LVL2
	);
	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_PASSWORD2,
		    ICNL9707_P_PASSWORD2_ENABLE_LVL2,
		    ICNL9707_P_PASSWORD2_ENABLE_LVL2
	); */

	mipi_dsi_dcs_write_seq(dsi,
		ICNL9707_CMD_UNLOCK_REGISTER,
		0xB4,
		0x4B
	);

	/* Set PWRCON_VCOM (-0.495V, -0.495V) */
	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_PWRCON_VCOM,
			ICNL9707_P_PWRCON_VCOM_0495V,
			ICNL9707_P_PWRCON_VCOM_0495V,
			0x00
	);

	/* Map ASG output signals */
	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_CGOUTR,
		    ICNL9707_P_CGOUT_GSP7, ICNL9707_P_CGOUT_GSP5,
		    ICNL9707_P_CGOUT_GCK7, ICNL9707_P_CGOUT_GCK5,
		    ICNL9707_P_CGOUT_GCK3, ICNL9707_P_CGOUT_GCK1,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_VGL,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GSP1, ICNL9707_P_CGOUT_GSP3,
			ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND
	);
	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_CGOUTL,
		    ICNL9707_P_CGOUT_GSP8, ICNL9707_P_CGOUT_GSP6,
		    ICNL9707_P_CGOUT_GCK8, ICNL9707_P_CGOUT_GCK6,
		    ICNL9707_P_CGOUT_GCK4, ICNL9707_P_CGOUT_GCK2,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_VGL,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_VGL, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND,
		    ICNL9707_P_CGOUT_GSP2, ICNL9707_P_CGOUT_GSP4,
			ICNL9707_P_CGOUT_GND, ICNL9707_P_CGOUT_GND
	);

	/* Undocumented commands provided by the vendor */
	// mipi_dsi_dcs_write_seq(dsi, 0xB0, 0x54, 0x32, 0x23, 0x45, 0x44, 0x44, 0x44, 0x44, 0x90, 0x01, 0x90, 0x01);
	// mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x32, 0x84, 0x02, 0x83, 0x30, 0x01, 0x6B, 0x01);
	// mipi_dsi_dcs_write_seq(dsi, 0xB2, 0x73);

	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_PWRCON_REG,
		    0x43, 0x0E, 0x0E, 0x50, 0x26,
		    0x1D, 0x00, 0x14, 0x42, 0x03
	);

	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_PWRCON_SEQ,
		    0x01, 0x01, 0x09, 0x11, 0x0D, 0x55,
		    0x19, 0x19, 0x00, 0x1D, 0x00, 0x00,
		    0x00, 0x00, 0x02, 0x02, 0xF7, 0x38
	);

	// mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_CLK, 0x11, 0x01, 0xFF);
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_CLK, 0x23, 0x01, 0x30, 0xCC);

	/* Disable abnormal power-off flag */
	// mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_BTA, 0xA1, 0x20, 0xFF, 0x44);
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_BTA, 0xA0, 0x22, 0x00, 0x44);

	// mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_MODE, 0x12, 0xB3);
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PWRCON_MODE, 0x12, 0x33);

	/* Set timing - VBP, VFP, VSW, HBP, HFP, HSW */
	// mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_TCON, 0x00, 0x0C, 0x10, 0x04, 0x00, 0x0C, 0x10, 0x04);

	mipi_dsi_dcs_write_seq(dsi,
		ICNL9707_CMD_TCON,
		((CWD686_VBP & 0xF00) >> 7) | ((CWD686_VFP & 0xF00) >> 8), // VBP / VFP high bits
		CWD686_VBP & 0xFF, // VBP
		CWD686_VFP & 0xFF, // VFP
		CWD686_VSW, // VSW
		((CWD686_HBP & 0xF00) >> 7) | ((CWD686_HFP & 0xF00) >> 8), // HBP / HFP high bits
		CWD686_HBP & 0xFF, // HBP
		CWD686_HFP & 0xFF, // HFP
		CWD686_HSW // HSW
	);

	/* Set resolution */
	mipi_dsi_dcs_write_seq(dsi,
		ICNL9707_CMD_TCON2,
		(((CWD686_VPX / 2) >> 4) & 0x30) | ICNL9707_TCON2_600RGB,
		(CWD686_VPX / 2) & 0xFF
	);

	/* Set frame blanking */
	mipi_dsi_dcs_write_seq(dsi,
		ICNL9707_CMD_TCON3,
		0x22,
		ICNL9707_TCON3_B4_EOR
	);

	/* Set the src state */
	mipi_dsi_dcs_write_seq(dsi,
		ICNL9707_CMD_SRCCON,
		0x45,
		0x2B,
		ICNL9707_SRCCON_ZSHIFT_DISABLE | ICNL9707_SRCCON_ZLINE_DISABLE,
		0x00,
		0x02
	);

	/* Another undocumented command */
	// mipi_dsi_dcs_write_seq(dsi, 0xC5, 0x00);

	/* Set failure state dection time - defaults to 0x10 (12.8us) but we maxed it out at 300us */
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_ETC, 0x37, 0xFF, 0xFF);

	/* Another set of undocumented commands */
	// mipi_dsi_dcs_write_seq(dsi, 0xD2, 0x63, 0x0B, 0x08, 0x88);
	// mipi_dsi_dcs_write_seq(dsi, 0xD3, 0x01, 0x00, 0x00, 0x01, 0x01, 0x37, 0x25, 0x38, 0x31, 0x06, 0x07);

	/* Set Gamma to 2.2 */
	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_SET_GAMMA,
		    0x7C, 0x6A, 0x5D, 0x53, 0x53, 0x45, 0x4B,
		    0x35, 0x4D, 0x4A, 0x49, 0x66, 0x53, 0x57,
		    0x4A, 0x48, 0x3B, 0x2A, 0x06, 0x7C, 0x6A,
		    0x5D, 0x53, 0x53, 0x45, 0x4B, 0x35, 0x4D,
		    0x4A, 0x49, 0x66, 0x53, 0x57, 0x4A, 0x48,
		    0x3B, 0x2A, 0x06
	);

	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_SRC_TIM, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x01);

	mipi_dsi_dcs_write_seq(dsi,
			MIPI_DCS_SET_ADDRESS_MODE,
		    ICNL9707_MADCTL_ML | ICNL9707_MADCTL_MH | ICNL9707_MADCTL_RGB
	);

	/* Enable tearing mode at VBLANK */
	err = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (err) {
		dev_err(ctx->dev, "failed to enable vblank TE (%d)\n", err);
		return err;
	}

	/* Disable access to Level 2 registers */

	mipi_dsi_dcs_write_seq(dsi,
			ICNL9707_CMD_UNLOCK_REGISTER,
			0x00,
			0x00
	);

	/*
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PASSWORD2,
		     ICNL9707_P_PASSWORD2_DEFAULT,
		     ICNL9707_P_PASSWORD2_DEFAULT);
	mipi_dsi_dcs_write_seq(dsi, ICNL9707_CMD_PASSWORD1,
		     ICNL9707_P_PASSWORD1_DEFAULT,
		     ICNL9707_P_PASSWORD1_DEFAULT);
	*/
	return 0;
}

static int cwd686_unprepare(struct drm_panel *panel)
{
	struct cwd686 *ctx = panel_to_cwd686(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	if (!ctx->prepared)
		return 0;

	err = mipi_dsi_dcs_set_display_off(dsi);
	if (err) {
		dev_err(ctx->dev, "failed to turn display off (%d)\n", err);
		return err;
	}

	err = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (err) {
		dev_err(ctx->dev, "failed to enter sleep mode (%d)\n", err);
		return err;
	}

	msleep(120);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vci);

	ctx->prepared = false;

	return 0;
}

static int cwd686_prepare(struct drm_panel *panel)
{
	struct cwd686 *ctx = panel_to_cwd686(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	if (ctx->prepared) {
		return 0;
	}

	err = regulator_enable(ctx->iovcc);
	if (err < 0) {
		dev_err(ctx->dev, "failed to enable iovcc supply: %d\n", err);
		return err;
	}

	err = regulator_enable(ctx->vci);
	if (err < 0) {
		dev_err(ctx->dev, "failed to enable vci supply: %d\n", err);
		goto disable_iovcc;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	/* T2 */
	msleep(10);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	/* T3 */
	msleep(20);

	cwd686_ids_show(ctx);

	// Send initialization code
	err = cwd686_init_sequence(ctx);
	if (err) {
		dev_err(ctx->dev, "failed to initialize display (%d)\n", err);
		goto disable_vci;
	}

	// Exit sleep mode command
	err = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (err) {
		dev_err(ctx->dev, "failed to exit sleep mode (%d)\n", err);
		goto disable_vci;
	}
	/* T6 - wait until first video packet is allowed */
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(dsi);
	if (err) {
		dev_err(ctx->dev, "failed to turn display on (%d)\n", err);
		goto disable_vci;
	}
	msleep(20);

	ctx->prepared = true;

	return 0;

disable_vci:
	regulator_disable(ctx->vci);
disable_iovcc:
	regulator_disable(ctx->iovcc);

	return err;
}

static int cwd686_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct cwd686 *ctx = panel_to_cwd686(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	/* Set up connector's "panel orientation" property */
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static enum drm_panel_orientation cwd686_get_orientation(struct drm_panel *panel)
{
	struct cwd686 *ctx = panel_to_cwd686(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs cwd686_drm_funcs = {
	.unprepare = cwd686_unprepare,
	.prepare = cwd686_prepare,
	.get_modes = cwd686_get_modes,
	.get_orientation = cwd686_get_orientation,
};

static int cwd686_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct cwd686 *ctx;
	int err;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_LPM |
			MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		err = PTR_ERR(ctx->reset_gpio);
		return dev_err_probe(dev, err, "Failed to request GPIO (%d)\n", err);
	}

	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci)) {
		err = PTR_ERR(ctx->vci);
		return dev_err_probe(dev, err, "Failed to request vci regulator: %d\n", err);
	}

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc)) {
		err = PTR_ERR(ctx->iovcc);
		return dev_err_probe(dev, err, "Failed to request iovcc regulator: %d\n", err);
	}

	err = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (err) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, err);
		return err;
	}

	drm_panel_init(&ctx->panel, dev, &cwd686_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	err = drm_panel_of_backlight(&ctx->panel);
	if (err)
		return dev_err_probe(dev, err, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", err);
		drm_panel_remove(&ctx->panel);
		return err;
	}

	return 0;
}

static void cwd686_remove(struct mipi_dsi_device *dsi)
{
	struct cwd686 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id cwd686_of_match[] = {
	{ .compatible = "cw,cwd686" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cwd686_of_match);

static struct mipi_dsi_driver cwd686_driver = {
	.probe = cwd686_probe,
	.remove = cwd686_remove,
	.driver = {
		.name = "panel-cwd686",
		.of_match_table = cwd686_of_match,
	},
};
module_mipi_dsi_driver(cwd686_driver);

MODULE_AUTHOR("Pinfan Zhu <zhu@clockworkpi.com>");
MODULE_AUTHOR("Max Fierke <max@maxfierke.com>");
MODULE_DESCRIPTION("ClockworkPi CWD686 panel driver");
MODULE_LICENSE("GPL");