/*
 * arch/arm/mach-tegra/panel-s-wqxga-10-1.c
 *
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <mach/dc.h>
#include <mach/iomap.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/tegra_pwm_bl.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm_backlight.h>
#include <linux/max8831_backlight.h>
#include <linux/leds.h>
#include <linux/ioport.h>
#include <linux/lp855x.h>
#include <generated/mach-types.h>
#include "board.h"
#include "board-panel.h"
#include "devices.h"
#include "gpio-names.h"
#include "tegra11_host1x_devices.h"

#define TEGRA_DSI_GANGED_MODE	1

#define DSI_PANEL_RESET		0
#define DSI_PANEL_RST_GPIO	TEGRA_GPIO_PH3
#define DSI_PANEL_BL_PWM	TEGRA_GPIO_PH1

#define DC_CTRL_MODE	(TEGRA_DC_OUT_CONTINUOUS_MODE | \
			TEGRA_DC_OUT_INITIALIZED_MODE)

#define en_vdd_bl	TEGRA_GPIO_PG0
#define lvds_en		TEGRA_GPIO_PG3


static bool reg_requested;
static bool gpio_requested;
static struct platform_device *disp_device;
static struct regulator *avdd_lcd_3v3;
static struct regulator *vdd_lcd_bl;
static struct regulator *vdd_lcd_bl_en;
static struct regulator *dvdd_lcd_1v8;
static struct regulator *vdd_ds_1v8;
static struct regulator *vdd_lcd_hv;
static struct timespec timestamp_lcd_off;

static tegra_dc_bl_output dsi_s_wqxga_10_1_bl_output_measured = {
	0, 1, 2, 3, 4, 5, 6, 7,
	8, 9, 11, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 21, 22,
	23, 24, 25, 26, 27, 28, 29, 30,
	31, 32, 32, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 43, 44,
	45, 46, 47, 48, 49, 50, 51, 52,
	53, 54, 54, 55, 56, 57, 58, 59,
	60, 61, 62, 63, 63, 64, 65, 66,
	67, 68, 69, 70, 71, 72, 73, 74,
	75, 76, 77, 78, 79, 80, 80, 81,
	82, 83, 84, 85, 86, 87, 88, 89,
	90, 91, 92, 93, 94, 95, 96, 97,
	98, 99, 100, 101, 102, 103, 104, 105,
	106, 107, 108, 109, 110, 111, 112, 113,
	114, 115, 116, 117, 118, 119, 120, 121,
	122, 123, 124, 125, 126, 127, 128, 129,
	130, 131, 132, 133, 134, 135, 136, 137,
	138, 140, 141, 142, 143, 144, 145, 146,
	147, 148, 149, 150, 151, 152, 153, 154,
	155, 156, 157, 158, 159, 160, 161, 162,
	163, 164, 165, 166, 167, 168, 169, 170,
	171, 172, 173, 174, 175, 177, 178, 179,
	180, 181, 182, 183, 184, 185, 186, 187,
	188, 189, 190, 191, 192, 193, 194, 195,
	196, 197, 198, 200, 201, 202, 203, 204,
	205, 206, 207, 208, 209, 210, 211, 212,
	213, 214, 215, 217, 218, 219, 220, 221,
	222, 223, 224, 225, 226, 227, 228, 229,
	230, 231, 232, 234, 235, 236, 237, 238,
	239, 240, 241, 242, 243, 244, 245, 246,
	248, 249, 250, 251, 252, 253, 254, 255,
};

static struct tegra_dsi_cmd dsi_s_wqxga_10_1_init_cmd[] = {
	DSI_DLY_MS(300),
	DSI_CMD_SHORT(DSI_DCS_WRITE_0_PARAM, DSI_DCS_EXIT_SLEEP_MODE, 0x0),
	DSI_DLY_MS(20),
	DSI_CMD_SHORT(DSI_DCS_WRITE_0_PARAM, DSI_DCS_SET_DISPLAY_ON, 0x0),
};

static struct tegra_dsi_cmd dsi_s_wqxga_10_1_suspend_cmd[] = {
	DSI_CMD_SHORT(DSI_DCS_WRITE_0_PARAM, DSI_DCS_SET_DISPLAY_OFF, 0x0),
	DSI_DLY_MS(67),	/* 3 frame duration per the panel datasheet */
	DSI_CMD_SHORT(DSI_DCS_WRITE_0_PARAM, DSI_DCS_ENTER_SLEEP_MODE, 0x0),
	DSI_DLY_MS(200), /* 1 frame duration per the panel datasheet */
};

static struct tegra_dsi_out dsi_s_wqxga_10_1_pdata = {
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	.n_data_lanes = 2,
	.controller_vs = DSI_VS_0,
#else
	.controller_vs = DSI_VS_1,
#endif

	.n_data_lanes = 8,
	.video_burst_mode = TEGRA_DSI_VIDEO_NONE_BURST_MODE,
	.ganged_type = TEGRA_DSI_GANGED_SYMMETRIC_EVEN_ODD,

	.pixel_format = TEGRA_DSI_PIXEL_FORMAT_24BIT_P,
	.refresh_rate = 60,
	.virtual_channel = TEGRA_DSI_VIRTUAL_CHANNEL_0,

	.dsi_instance = DSI_INSTANCE_0,

	.panel_reset = DSI_PANEL_RESET,
	.power_saving_suspend = true,
	.video_data_type = TEGRA_DSI_VIDEO_TYPE_VIDEO_MODE,
	.video_clock_mode = TEGRA_DSI_VIDEO_CLOCK_TX_ONLY,
	.dsi_init_cmd = dsi_s_wqxga_10_1_init_cmd,
	.n_init_cmd = ARRAY_SIZE(dsi_s_wqxga_10_1_init_cmd),
	.n_suspend_cmd = ARRAY_SIZE(dsi_s_wqxga_10_1_suspend_cmd),
	.dsi_suspend_cmd = dsi_s_wqxga_10_1_suspend_cmd,
};

static int dalmore_dsi_regulator_get(struct device *dev)
{
	int err = 0;

	if (reg_requested)
		return 0;

	dvdd_lcd_1v8 = regulator_get(dev, "dvdd_lcd");
	if (IS_ERR_OR_NULL(dvdd_lcd_1v8)) {
		pr_err("dvdd_lcd regulator get failed\n");
		err = PTR_ERR(dvdd_lcd_1v8);
		dvdd_lcd_1v8 = NULL;
		goto fail;
	}

	avdd_lcd_3v3 = regulator_get(dev, "avdd_lcd");
	if (IS_ERR_OR_NULL(avdd_lcd_3v3)) {
		pr_err("avdd_lcd regulator get failed\n");
		err = PTR_ERR(avdd_lcd_3v3);
		avdd_lcd_3v3 = NULL;
		goto fail;
	}

	vdd_lcd_bl = regulator_get(dev, "vdd_lcd_bl");
	if (IS_ERR_OR_NULL(vdd_lcd_bl)) {
		pr_err("vdd_lcd_bl regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl);
		vdd_lcd_bl = NULL;
		goto fail;
	}

	vdd_lcd_bl_en = regulator_get(dev, "vdd_lcd_bl_en");
	if (IS_ERR_OR_NULL(vdd_lcd_bl_en)) {
		pr_err("vdd_lcd_bl_en regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl_en);
		vdd_lcd_bl_en = NULL;
		goto fail;
	}
	reg_requested = true;
	return 0;
fail:
	return err;
}

static int dalmore_dsi_gpio_get(void)
{
	int err = 0;

	if (gpio_requested)
		return 0;

#if DSI_PANEL_RESET
	err = gpio_request(DSI_PANEL_RST_GPIO, "panel rst");
	if (err < 0) {
		pr_err("panel reset gpio request failed\n");
		goto fail;
	}
#endif

	/* free pwm GPIO */
	err = gpio_request(DSI_PANEL_BL_PWM, "panel pwm");
	if (err < 0) {
		pr_err("panel pwm gpio request failed\n");
		goto fail;
	}
	gpio_free(DSI_PANEL_BL_PWM);
	gpio_requested = true;
	return 0;
fail:
	return err;
}

static int macallan_dsi_regulator_get(struct device *dev)
{
	int err = 0;

	if (reg_requested)
		return 0;

	avdd_lcd_3v3 = regulator_get(dev, "avdd_lcd");
	if (IS_ERR_OR_NULL(avdd_lcd_3v3)) {
		pr_err("avdd_lcd regulator get failed\n");
		err = PTR_ERR(avdd_lcd_3v3);
		avdd_lcd_3v3 = NULL;
		goto fail;
	}

	vdd_lcd_hv = regulator_get(dev, "vdd_lcd_hv");
	if (IS_ERR_OR_NULL(vdd_lcd_hv)) {
		pr_err("vdd_lcd_hv regulator get failed\n");
		err = PTR_ERR(vdd_lcd_hv);
		vdd_lcd_hv = NULL;
		goto fail;
	}

	vdd_lcd_bl_en = regulator_get(dev, "vdd_lcd_bl_en");
	if (IS_ERR_OR_NULL(vdd_lcd_bl_en)) {
		pr_err("vdd_lcd_bl_en regulator get failed\n");
		err = PTR_ERR(vdd_lcd_bl_en);
		vdd_lcd_bl_en = NULL;
		goto fail;
	}
	reg_requested = true;
	return 0;
fail:
	return err;
}

static int macallan_dsi_gpio_get(void)
{
	int err = 0;

	if (gpio_requested)
		return 0;

#if DSI_PANEL_RESET
	err = gpio_request(DSI_PANEL_RST_GPIO, "panel rst");
	if (err < 0) {
		pr_err("panel reset gpio request failed\n");
		goto fail;
	}
#endif

	/* free pwm GPIO */
	err = gpio_request(DSI_PANEL_BL_PWM, "panel pwm");
	if (err < 0) {
		pr_err("panel pwm gpio request failed\n");
		goto fail;
	}
	gpio_free(DSI_PANEL_BL_PWM);
	gpio_requested = true;
	return 0;
fail:
	return err;
}
static int dsi_s_wqxga_10_1_enable(struct device *dev)
{
	int err = 0, ms_to_delay = 0;
	struct timespec now;

	if (machine_is_dalmore())
		err = dalmore_dsi_regulator_get(dev);
	else if (machine_is_macallan())
		err = macallan_dsi_regulator_get(dev);
	if (err < 0) {
		pr_err("dsi regulator get failed\n");
		goto fail;
	}
	if (machine_is_dalmore())
		err = dalmore_dsi_gpio_get();
	else if (machine_is_macallan())
		err = macallan_dsi_gpio_get();
	if (err < 0) {
		pr_err("dsi gpio request failed\n");
		goto fail;
	}

	/* According to panel spec., the panel needs at least 1s (we give it 1.5s) for dis-charging 
	    before transiting power state from off to on.
	*/
	read_persistent_clock(&now);
	now = timespec_sub(now, timestamp_lcd_off);
	ms_to_delay = 1500 - (now.tv_sec * 1000 + (now.tv_nsec / NSEC_PER_MSEC));
	if (ms_to_delay > 0) {
		printk("%s: delay %dms before turning on panel...\r\n", __func__, ms_to_delay);
		msleep(ms_to_delay);
	}

	if (vdd_ds_1v8) {
		err = regulator_enable(vdd_ds_1v8);
		if (err < 0) {
			pr_err("vdd_ds_1v8 regulator enable failed\n");
			goto fail;
		}
	}

	if (dvdd_lcd_1v8) {
		err = regulator_enable(dvdd_lcd_1v8);
		if (err < 0) {
			pr_err("dvdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (avdd_lcd_3v3) {
		err = regulator_enable(avdd_lcd_3v3);
		if (err < 0) {
			pr_err("avdd_lcd regulator enable failed\n");
			goto fail;
		}
	}

	if (vdd_lcd_hv) {
		err = regulator_enable(vdd_lcd_hv);
		if (err < 0) {
			pr_err("vdd_lcd_hv regulator enable failed\n");
			goto fail;
		}
	}

	msleep(300);
#if DSI_PANEL_RESET
	gpio_direction_output(DSI_PANEL_RST_GPIO, 1);
	usleep_range(1000, 5000);
	gpio_set_value(DSI_PANEL_RST_GPIO, 0);
	usleep_range(1000, 5000);
	gpio_set_value(DSI_PANEL_RST_GPIO, 1);
	msleep(20);
#endif

	return 0;
fail:
	return err;
}

static int dsi_s_wqxga_10_1_disable(void)
{
	if (vdd_lcd_hv)
		regulator_disable(vdd_lcd_hv);

	if (avdd_lcd_3v3)
		regulator_disable(avdd_lcd_3v3);

	if (dvdd_lcd_1v8)
		regulator_disable(dvdd_lcd_1v8);

	if (vdd_ds_1v8)
		regulator_disable(vdd_ds_1v8);

	/* Remember the time when the panel being power down */
	read_persistent_clock(&timestamp_lcd_off);

	return 0;
}

static int dsi_s_wqxga_10_1_postsuspend(void)
{
	return 0;
}

static void bl_work_handler(struct work_struct *work);
static DECLARE_DELAYED_WORK(bl_work, bl_work_handler);

static int dsi_s_wqxga_10_1_prepoweroff(void)
{	
	cancel_delayed_work_sync(&bl_work);
	
	if (vdd_lcd_bl_en && regulator_is_enabled(vdd_lcd_bl_en))
		regulator_disable(vdd_lcd_bl_en);

	if (vdd_lcd_bl && regulator_is_enabled(vdd_lcd_bl))
		regulator_disable(vdd_lcd_bl);
	
	return 0;
}

static void bl_work_handler(struct work_struct *work)
{
	int err;
	
	if (vdd_lcd_bl) {
		err = regulator_enable(vdd_lcd_bl);
		if (err < 0) {
			pr_err("vdd_lcd_bl regulator enable failed %d\n", err);
		}
	}

	if (vdd_lcd_bl_en) {
		err = regulator_enable(vdd_lcd_bl_en);
		if (err < 0) {
			pr_err("vdd_lcd_bl_en regulator enable failed %d\n", err);
		}
	}
}


static int dsi_s_wqxga_10_1_postpoweron(void)
{
	cancel_delayed_work_sync(&bl_work);
	schedule_delayed_work(&bl_work, 0.1 * HZ);

	return 0;
}

static struct tegra_dc_mode dsi_s_wqxga_10_1_modes[] = {
	{
		.pclk = 268460000,
		.h_ref_to_sync = 4,
		.v_ref_to_sync = 1,
		.h_sync_width = 32,
		.v_sync_width = 6,
		.h_back_porch = 80,
		.v_back_porch = 37,
		.h_active = 2560,
		.v_active = 1600,
		.h_front_porch = 48,
		.v_front_porch = 3,
	},
};

#ifdef CONFIG_TEGRA_DC_CMU
static struct tegra_dc_cmu dsi_s_wqxga_10_1_cmu = {
	/* lut1 maps sRGB to linear space. */
	{
	0,    1,    2,    4,    5,    6,    7,    9,
	10,   11,   12,   14,   15,   16,   18,   20,
	21,   23,   25,   27,   29,   31,   33,   35,
	37,   40,   42,   45,   48,   50,   53,   56,
	59,   62,   66,   69,   72,   76,   79,   83,
	87,   91,   95,   99,   103,  107,  112,  116,
	121,  126,  131,  136,  141,  146,  151,  156,
	162,  168,  173,  179,  185,  191,  197,  204,
	210,  216,  223,  230,  237,  244,  251,  258,
	265,  273,  280,  288,  296,  304,  312,  320,
	329,  337,  346,  354,  363,  372,  381,  390,
	400,  409,  419,  428,  438,  448,  458,  469,
	479,  490,  500,  511,  522,  533,  544,  555,
	567,  578,  590,  602,  614,  626,  639,  651,
	664,  676,  689,  702,  715,  728,  742,  755,
	769,  783,  797,  811,  825,  840,  854,  869,
	884,  899,  914,  929,  945,  960,  976,  992,
	1008, 1024, 1041, 1057, 1074, 1091, 1108, 1125,
	1142, 1159, 1177, 1195, 1213, 1231, 1249, 1267,
	1286, 1304, 1323, 1342, 1361, 1381, 1400, 1420,
	1440, 1459, 1480, 1500, 1520, 1541, 1562, 1582,
	1603, 1625, 1646, 1668, 1689, 1711, 1733, 1755,
	1778, 1800, 1823, 1846, 1869, 1892, 1916, 1939,
	1963, 1987, 2011, 2035, 2059, 2084, 2109, 2133,
	2159, 2184, 2209, 2235, 2260, 2286, 2312, 2339,
	2365, 2392, 2419, 2446, 2473, 2500, 2527, 2555,
	2583, 2611, 2639, 2668, 2696, 2725, 2754, 2783,
	2812, 2841, 2871, 2901, 2931, 2961, 2991, 3022,
	3052, 3083, 3114, 3146, 3177, 3209, 3240, 3272,
	3304, 3337, 3369, 3402, 3435, 3468, 3501, 3535,
	3568, 3602, 3636, 3670, 3705, 3739, 3774, 3809,
	3844, 3879, 3915, 3950, 3986, 4022, 4059, 4095,
	},
	/* csc */
	{
		0x12D, 0x3BE, 0x014,
		0x3F6, 0x116, 0x3E6,
		0x3FE, 0x3F8, 0x0DB,
	},
	/* lut2 maps linear space to sRGB */
	{
		0,0,1,1,2,3,3,4,
		4,5,6,6,7,7,8,9,
		9,10,10,11,11,12,12,13,
		13,14,15,15,16,16,17,17,
		18,18,19,19,19,20,20,21,
		21,22,22,23,23,24,24,25,
		25,25,26,26,27,27,27,28,
		28,29,29,30,30,30,31,31,
		31,32,32,33,33,33,34,34,
		34,35,35,36,36,36,37,37,
		37,38,38,38,39,39,39,40,
		40,40,41,41,41,41,42,42,
		42,43,43,43,44,44,44,44,
		45,45,45,45,46,46,46,47,
		47,47,47,48,48,48,48,49,
		49,49,49,49,50,50,50,50,
		51,51,51,51,51,52,52,52,
		52,52,53,53,53,53,53,54,
		54,54,54,54,55,55,55,55,
		55,55,56,56,56,56,56,56,
		57,57,57,57,57,57,58,58,
		58,58,58,58,58,59,59,59,
		59,59,59,59,60,60,60,60,
		60,60,60,61,61,61,61,61,
		61,61,62,62,62,62,62,62,
		62,62,63,63,63,63,63,63,
		63,63,64,64,64,64,64,64,
		64,65,65,65,65,65,65,65,
		65,66,66,66,66,66,66,66,
		67,67,67,67,67,67,67,67,
		68,68,68,68,68,68,68,68,
		69,69,69,69,69,69,69,70,
		70,70,70,70,70,70,70,71,
		71,71,71,71,71,71,71,72,
		72,72,72,72,72,72,72,73,
		73,73,73,73,73,73,74,74,
		74,74,74,74,74,74,75,75,
		75,75,75,75,75,75,76,76,
		76,76,76,76,76,76,77,77,
		77,77,77,77,77,77,77,78,
		78,78,78,78,78,78,78,79,
		79,79,79,79,79,79,79,79,
		80,80,80,80,80,80,80,80,
		80,81,81,81,81,81,81,81,
		81,81,82,82,82,82,82,82,
		82,82,82,83,83,83,83,83,
		83,83,83,83,83,84,84,84,
		84,84,84,84,84,84,84,85,
		85,85,85,85,85,85,85,85,
		85,86,86,86,86,86,86,86,
		86,86,86,87,87,87,87,87,
		87,87,87,87,87,87,88,88,
		88,88,88,88,88,88,88,88,
		88,89,89,89,89,89,89,89,
		89,89,89,89,90,90,90,90,
		90,90,90,90,90,90,90,91,
		91,91,91,91,91,91,91,91,
		91,91,91,92,92,92,92,92,
		92,92,92,92,92,92,93,93,
		93,93,93,93,93,93,93,93,
		93,93,94,94,94,94,94,94,
		94,94,94,94,94,94,95,95,
		95,95,95,95,95,95,95,95,
		95,95,96,96,96,96,96,96,
		97,97,98,99,99,100,101,101,
		102,103,103,104,105,105,106,107,
		107,108,108,109,110,110,111,111,
		112,113,113,114,114,115,115,116,
		117,117,118,118,119,119,120,120,
		121,121,122,122,123,123,124,124,
		125,125,126,126,127,127,128,128,
		129,129,129,130,130,131,131,132,
		132,133,133,134,134,134,135,135,
		136,136,137,137,137,138,138,139,
		139,140,140,140,141,141,142,142,
		143,143,143,144,144,145,145,145,
		146,146,147,147,148,148,148,149,
		149,150,150,150,151,151,152,152,
		152,153,153,154,154,154,155,155,
		156,156,156,157,157,157,158,158,
		159,159,159,160,160,160,161,161,
		162,162,162,163,163,163,164,164,
		165,165,165,166,166,166,167,167,
		168,168,168,169,169,169,170,170,
		170,171,171,172,172,172,173,173,
		173,174,174,174,175,175,176,176,
		176,177,177,177,178,178,178,179,
		179,180,180,180,181,181,181,182,
		182,182,183,183,183,184,184,185,
		185,185,186,186,186,187,187,187,
		188,188,188,189,189,189,190,190,
		191,191,191,192,192,192,193,193,
		193,194,194,194,195,195,195,196,
		196,196,197,197,197,198,198,198,
		199,199,199,199,200,200,200,201,
		201,201,202,202,202,203,203,203,
		204,204,204,204,205,205,205,206,
		206,206,207,207,207,207,208,208,
		208,209,209,209,209,210,210,210,
		211,211,211,211,212,212,212,213,
		213,213,213,214,214,214,214,215,
		215,215,216,216,216,216,217,217,
		217,217,218,218,218,218,219,219,
		219,220,220,220,220,221,221,221,
		221,222,222,222,222,223,223,223,
		223,224,224,224,224,225,225,225,
		225,226,226,226,226,227,227,227,
		227,228,228,228,228,229,229,229,
		229,230,230,230,230,231,231,231,
		231,232,232,232,232,233,233,233,
		233,234,234,234,234,235,235,235,
		235,236,236,236,236,237,237,237,
		237,238,238,238,238,239,239,239,
		239,240,240,240,241,241,241,241,
		242,242,242,242,243,243,243,244,
		244,244,244,245,245,245,245,246,
		246,246,247,247,247,247,248,248,
		248,249,249,249,250,250,250,250,
		251,251,251,252,252,252,253,253,
		253,254,254,254,254,255,255,255,
	},
};
#endif

static int dsi_s_wqxga_10_1_bl_notify(struct device *unused, int brightness)
{
	int cur_sd_brightness = atomic_read(&sd_brightness);

	/* SD brightness is a percentage */
	brightness = (brightness * cur_sd_brightness) / 255;

	/* Apply any backlight response curve */
	if (brightness > 255)
		pr_info("Error: Brightness > 255!\n");
	else
		brightness = dsi_s_wqxga_10_1_bl_output_measured[brightness];

	return brightness;
}

static int dsi_s_wqxga_10_1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &disp_device->dev;
}

static struct platform_pwm_backlight_data dsi_s_wqxga_10_1_bl_data = {
	.pwm_id		= 1,
	.max_brightness	= 255,
	.dft_brightness	= 224,
	.pwm_period_ns	= 1000000,
	.pwm_gpio	= DSI_PANEL_BL_PWM,
	.notify		= dsi_s_wqxga_10_1_bl_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= dsi_s_wqxga_10_1_check_fb,
};

static struct platform_device __maybe_unused
		dsi_s_wqxga_10_1_bl_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &dsi_s_wqxga_10_1_bl_data,
	},
};

static struct platform_device __maybe_unused
			*dsi_s_wqxga_10_1_bl_devices[] __initdata = {
	&tegra_pwfm1_device,
	&dsi_s_wqxga_10_1_bl_device,
};

#define EPROM_CFG1_ADDR	0xA1
#define EPROM_CFG1_VAL	0xBF
static struct lp855x_rom_data lp8552_eprom_arr[] = {
	{EPROM_CFG1_ADDR, EPROM_CFG1_VAL},	/* Set max. LED current as 20mA */
};

static struct lp855x_platform_data lp8552_pdata = {
	.mode = PWM_BASED,
	.device_control = PWM_CONFIG(LP8556),
	.load_new_rom_data = 1,
	.size_program = ARRAY_SIZE(lp8552_eprom_arr),
	.rom_data = lp8552_eprom_arr,
};

static struct i2c_board_info dora_lp8556_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("lp8556", 0x2C),
		.platform_data = &lp8552_pdata,
	},
};

static int __init dsi_s_wqxga_10_1_register_bl_dev(void)
{
	int err = 0;
	err = platform_add_devices(dsi_s_wqxga_10_1_bl_devices,
				ARRAY_SIZE(dsi_s_wqxga_10_1_bl_devices));
	if (err) {
		pr_err("disp1 bl device registration failed");
		return err;
	}
	err = i2c_register_board_info(0, dora_lp8556_board_info, 
				ARRAY_SIZE(dora_lp8556_board_info));
	if (err) {
		pr_err("disp1 lp8556 device registration failed");
		return err;
	}
	return err;
}

static void dsi_s_wqxga_10_1_set_disp_device(
	struct platform_device *dalmore_display_device)
{
	disp_device = dalmore_display_device;
}
/* Sharp is configured in Ganged mode */
static void dsi_s_wqxga_10_1_resources_init(struct resource *
resources, int n_resources)
{
	int i;
	for (i = 0; i < n_resources; i++) {
		struct resource *r = &resources[i];
		if (resource_type(r) == IORESOURCE_MEM &&
			!strcmp(r->name, "ganged_dsia_regs")) {
			r->start = TEGRA_DSI_BASE;
			r->end = TEGRA_DSI_BASE + TEGRA_DSI_SIZE - 1;
		}
		if (resource_type(r) == IORESOURCE_MEM &&
			!strcmp(r->name, "ganged_dsib_regs")) {
			r->start = TEGRA_DSIB_BASE;
			r->end = TEGRA_DSIB_BASE + TEGRA_DSIB_SIZE - 1;
		}
	}
}

static void dsi_s_wqxga_10_1_dc_out_init(struct tegra_dc_out *dc)
{
	dc->dsi = &dsi_s_wqxga_10_1_pdata;
	dc->parent_clk = "pll_d_out0";
	dc->modes = dsi_s_wqxga_10_1_modes;
	dc->n_modes = ARRAY_SIZE(dsi_s_wqxga_10_1_modes);
	dc->enable = dsi_s_wqxga_10_1_enable;
	dc->disable = dsi_s_wqxga_10_1_disable;
	dc->postsuspend	= dsi_s_wqxga_10_1_postsuspend,
	dc->prepoweroff = dsi_s_wqxga_10_1_prepoweroff,
	dc->postpoweron = dsi_s_wqxga_10_1_postpoweron,
	dc->width = 216;
	dc->height = 135;
	dc->flags = DC_CTRL_MODE;
}

static void dsi_s_wqxga_10_1_fb_data_init(struct tegra_fb_data *fb)
{
	fb->xres = dsi_s_wqxga_10_1_modes[0].h_active;
	fb->yres = dsi_s_wqxga_10_1_modes[0].v_active;
}

static void
dsi_s_wqxga_10_1_sd_settings_init(struct tegra_dc_sd_settings *settings)
{
	settings->bl_device_name = "pwm-backlight";
}

static void dsi_s_wqxga_10_1_cmu_init(struct tegra_dc_platform_data *pdata)
{
	pdata->cmu = &dsi_s_wqxga_10_1_cmu;
}

struct tegra_panel __initdata dsi_s_wqxga_10_1 = {
	.init_sd_settings = dsi_s_wqxga_10_1_sd_settings_init,
	.init_dc_out = dsi_s_wqxga_10_1_dc_out_init,
	.init_fb_data = dsi_s_wqxga_10_1_fb_data_init,
	.init_resources = dsi_s_wqxga_10_1_resources_init,
	.register_bl_dev = dsi_s_wqxga_10_1_register_bl_dev,
	.init_cmu_data = dsi_s_wqxga_10_1_cmu_init,
	.set_disp_device = dsi_s_wqxga_10_1_set_disp_device,
};
EXPORT_SYMBOL(dsi_s_wqxga_10_1);

