// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <soc/oplus/system/oplus_mm_kevent_fb.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6363/registers.h>
#include "../../../../misc/mediatek/include/mt-plat/mtk_boot_common.h"

#include <soc/oplus/device_info.h>
#include "ktz8868.h"
#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../mediatek/mediatek_v2/mtk_log.h"
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
#include "../mediatek/mediatek_v2/mtk_disp_notify.h"
#define LCD_CTL_CS_OFF  0x1A
#define LCD_CTL_CS_ON  0x19
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int bpc;

	/**
	 * @width_mm: width of the panel's active display area
	 * @height_mm: height of the panel's active display area
	 */
	struct {
		unsigned int width_mm;
		unsigned int height_mm;
	} size;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	const struct panel_init_cmd *init_cmds;
	unsigned int lanes;
};

enum led_mode {
	LED_MODE_BLS_NONE = 0,
	LED_MODE_BLS_VIRTUAL,
	LED_MODE_BLS_CABC
};

struct lcm {
	struct device *dev;
	struct mipi_dsi_device *dsi;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *master_esd_gpio;
	struct gpio_desc *slave_esd_gpio;
	struct gpio_desc *bias_enp_en;
	struct gpio_desc *bias_enn_en;
	struct gpio_desc *bias_en;
	struct gpio_desc *lcm_vddi_en;
	struct regulator *reg;
	struct regmap *pmic_regmap;
	bool prepared;
	bool enabled;
	int error;
	unsigned int gate_ic;
	bool display_dual_swap;
	enum led_mode bl_mode;
};

#define lcm_dcs_write_seq(ctx, seq...)                                     \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})
#define lcm_dcs_write_seq_static(ctx, seq...)                              \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                      \
	})

#define  M_DELAY(n) usleep_range(n*1000, n*1000+100)
#define  U_DELAY(n) usleep_range(n, n+10)

#define HSA                (4)
#define HBP_90_48HZ        (60)
#define HBP_120_60HZ       (45)

#define HFP_90_48HZ        (217)
#define HFP_120_60HZ       (45)

#define VSA                (2)
#define VBP                (230)

#define VFP_120HZ          (56)
#define VFP_90HZ           (56)
#define VFP_60HZ           (2344)
#define VFP_48HZ           (2058)

#define VAC_FHD            (2000)
#define HAC_FHD            (2800)

#define LCM_LDO_KEEP_AWAKE_BIT BIT(3)

static int g_fps_current = 120;

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}


#define MAX_NORMAL_BRIGHTNESS			1884
#define MAX_HW_BRIGHTNESS			2047
static bool ktz8868_set_bl_flag = false;
unsigned int level_backup = 0;
#if IS_ENABLED(CONFIG_TOUCHPANEL_NOTIFY)
extern int (*tp_gesture_enable_notifier)(unsigned int tp_index);
#endif
static bool is_pd_with_guesture = false;
extern unsigned long esd_flag;
extern bool g_shutdown;
extern unsigned int oplus_display_brightness;
extern unsigned int oplus_max_normal_brightness;
static unsigned int backlight_map[] = {
	   0,    5,   10,   13,   14,   14,   14,   14,   14,   14,   14,   14,   14,   14,   14,   14,   14,   15,   16,   17,
	  18,   19,   20,   21,   22,   23,   24,   25,   26,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,
	  27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   27,   28,   28,   28,
	  28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,
	  28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,
	  28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,   28,
	  28,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,
	  29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,
	  29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,   29,
	  29,   29,   29,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   30,   31,   31,   31,   31,   31,   31,
	  31,   31,   31,   31,   31,   31,   32,   32,   32,   32,   32,   32,   32,   32,   32,   32,   32,   33,   33,   33,
	  33,   33,   33,   33,   33,   33,   33,   33,   34,   34,   34,   34,   34,   34,   34,   34,   34,   34,   34,   35,
	  35,   35,   35,   35,   35,   35,   35,   35,   35,   35,   35,   36,   36,   36,   36,   36,   36,   36,   36,   36,
	  36,   36,   37,   37,   37,   37,   37,   37,   37,   37,   37,   37,   37,   38,   38,   38,   38,   38,   38,   38,
	  38,   38,   38,   38,   39,   39,   39,   39,   39,   39,   39,   39,   39,   39,   40,   40,   40,   40,   40,   40,
	  40,   40,   40,   40,   40,   41,   41,   41,   41,   41,   41,   41,   41,   41,   41,   41,   42,   42,   42,   42,
	  42,   42,   42,   42,   42,   42,   42,   43,   43,   43,   43,   43,   43,   43,   43,   43,   43,   44,   44,   44,
	  44,   44,   44,   44,   44,   44,   45,   45,   45,   46,   46,   46,   47,   47,   47,   48,   48,   48,   49,   49,
	  49,   50,   50,   50,   51,   51,   51,   52,   52,   52,   53,   53,   53,   53,   54,   54,   54,   55,   55,   55,
	  56,   56,   56,   57,   57,   57,   58,   58,   58,   59,   59,   59,   60,   60,   60,   61,   61,   61,   62,   62,
	  62,   63,   63,   63,   64,   64,   64,   65,   65,   65,   66,   66,   66,   66,   67,   67,   67,   68,   68,   69,
	  69,   69,   70,   70,   71,   71,   71,   72,   72,   73,   73,   73,   74,   74,   75,   75,   75,   76,   76,   77,
	  77,   77,   78,   78,   79,   79,   79,   80,   80,   81,   81,   81,   82,   82,   83,   83,   84,   84,   84,   85,
	  85,   86,   86,   86,   87,   87,   88,   88,   88,   89,   89,   90,   90,   90,   91,   91,   92,   92,   92,   93,
	  93,   94,   94,   94,   95,   95,   96,   96,   97,   97,   98,   98,   99,   99,   99,  100,  100,  101,  101,  102,
	 102,  103,  103,  104,  104,  105,  105,  106,  106,  107,  107,  107,  108,  108,  109,  109,  110,  110,  111,  111,
	 112,  112,  113,  113,  114,  114,  115,  115,  116,  116,  116,  117,  117,  118,  118,  119,  119,  120,  120,  121,
	 121,  122,  122,  123,  123,  124,  124,  124,  125,  125,  126,  126,  127,  127,  128,  128,  129,  129,  130,  131,
	 131,  132,  132,  133,  133,  134,  134,  135,  135,  136,  136,  137,  137,  138,  138,  139,  140,  140,  141,  141,
	 142,  142,  143,  143,  144,  144,  145,  145,  146,  146,  147,  147,  148,  149,  149,  150,  150,  151,  151,  152,
	 152,  153,  153,  154,  154,  155,  155,  156,  156,  157,  158,  158,  159,  159,  160,  160,  161,  161,  162,  163,
	 163,  164,  165,  165,  166,  166,  167,  168,  168,  169,  170,  170,  171,  171,  172,  173,  173,  174,  175,  175,
	 176,  176,  177,  178,  178,  179,  180,  180,  181,  181,  182,  183,  183,  184,  185,  185,  186,  186,  187,  188,
	 188,  189,  190,  190,  191,  191,  192,  193,  193,  194,  195,  195,  196,  196,  197,  198,  198,  199,  200,  200,
	 201,  202,  202,  203,  204,  204,  205,  206,  207,  207,  208,  209,  209,  210,  211,  211,  212,  213,  213,  214,
	 215,  215,  216,  217,  217,  218,  219,  219,  220,  221,  221,  222,  223,  223,  224,  225,  225,  226,  227,  228,
	 228,  229,  230,  230,  231,  232,  232,  233,  234,  234,  235,  236,  236,  237,  238,  239,  239,  240,  241,  242,
	 242,  243,  244,  244,  245,  246,  247,  247,  248,  249,  250,  250,  251,  252,  253,  253,  254,  255,  255,  256,
	 257,  258,  258,  259,  260,  261,  261,  262,  263,  264,  264,  265,  266,  266,  267,  268,  269,  269,  270,  271,
	 272,  272,  273,  274,  275,  275,  276,  277,  278,  278,  279,  280,  281,  281,  282,  283,  284,  285,  285,  286,
	 287,  288,  289,  289,  290,  291,  292,  293,  293,  294,  295,  296,  297,  297,  298,  299,  300,  301,  301,  302,
	 303,  304,  304,  305,  306,  307,  308,  308,  309,  310,  311,  312,  312,  313,  314,  315,  316,  316,  317,  318,
	 319,  320,  320,  321,  322,  323,  324,  325,  326,  326,  327,  328,  329,  330,  331,  332,  332,  333,  334,  335,
	 336,  337,  338,  338,  339,  340,  341,  342,  343,  344,  344,  345,  346,  347,  348,  349,  350,  350,  351,  352,
	 353,  354,  355,  356,  356,  357,  358,  359,  360,  361,  362,  362,  363,  364,  365,  366,  367,  368,  369,  370,
	 371,  372,  373,  374,  375,  375,  376,  377,  378,  379,  380,  381,  382,  383,  384,  385,  386,  387,  387,  388,
	 389,  390,  391,  392,  393,  394,  395,  396,  397,  398,  399,  400,  400,  401,  402,  403,  404,  405,  406,  407,
	 408,  409,  410,  411,  412,  413,  414,  415,  416,  417,  418,  419,  420,  421,  422,  423,  424,  425,  426,  427,
	 428,  429,  430,  431,  432,  433,  434,  435,  436,  437,  438,  439,  440,  441,  442,  443,  444,  445,  446,  447,
	 448,  449,  450,  451,  452,  453,  454,  455,  456,  457,  458,  459,  460,  461,  462,  463,  464,  465,  466,  467,
	 468,  469,  470,  471,  472,  473,  474,  475,  476,  478,  479,  480,  481,  482,  483,  484,  485,  486,  487,  488,
	 489,  490,  491,  492,  493,  494,  495,  496,  497,  498,  499,  500,  501,  502,  503,  505,  506,  507,  508,  509,
	 510,  511,  512,  513,  515,  516,  517,  518,  519,  520,  521,  522,  524,  525,  526,  527,  528,  529,  530,  531,
	 533,  534,  535,  536,  537,  538,  539,  540,  542,  543,  544,  545,  546,  547,  548,  549,  551,  552,  553,  554,
	 555,  556,  557,  558,  560,  561,  562,  563,  564,  565,  567,  568,  569,  570,  571,  572,  574,  575,  576,  577,
	 578,  579,  581,  582,  583,  584,  585,  586,  588,  589,  590,  591,  592,  593,  595,  596,  597,  598,  599,  600,
	 602,  603,  604,  605,  606,  607,  609,  610,  611,  612,  613,  614,  616,  617,  618,  619,  620,  621,  623,  624,
	 625,  626,  627,  628,  630,  631,  632,  633,  634,  635,  637,  638,  639,  640,  641,  642,  644,  645,  646,  647,
	 648,  649,  651,  652,  653,  654,  655,  656,  658,  659,  660,  661,  663,  664,  665,  666,  668,  669,  670,  671,
	 673,  674,  675,  676,  678,  679,  680,  681,  683,  684,  685,  686,  688,  689,  690,  692,  693,  694,  695,  697,
	 698,  699,  700,  702,  703,  704,  705,  707,  708,  709,  710,  712,  713,  714,  716,  717,  718,  720,  721,  722,
	 724,  725,  726,  727,  729,  730,  731,  733,  734,  735,  737,  738,  739,  741,  742,  743,  745,  746,  747,  749,
	 750,  751,  753,  754,  755,  756,  758,  759,  760,  762,  763,  764,  766,  767,  768,  770,  771,  773,  774,  775,
	 777,  778,  779,  781,  782,  783,  785,  786,  788,  789,  790,  792,  793,  794,  796,  797,  798,  800,  801,  803,
	 804,  805,  807,  808,  809,  811,  812,  813,  815,  816,  818,  819,  820,  822,  823,  825,  826,  828,  829,  830,
	 832,  833,  835,  836,  838,  839,  841,  842,  844,  845,  847,  848,  850,  851,  853,  854,  856,  857,  859,  860,
	 861,  863,  864,  866,  867,  869,  870,  872,  873,  875,  876,  878,  879,  881,  882,  883,  885,  886,  888,  889,
	 891,  892,  893,  895,  896,  898,  899,  901,  902,  903,  905,  906,  908,  909,  911,  912,  913,  915,  916,  918,
	 919,  921,  922,  923,  925,  926,  928,  929,  931,  932,  933,  935,  937,  938,  940,  941,  943,  945,  946,  948,
	 949,  951,  953,  954,  956,  957,  959,  961,  962,  964,  965,  967,  969,  970,  972,  973,  975,  977,  978,  980,
	 981,  983,  985,  986,  988,  989,  991,  993,  994,  996,  997,  999, 1000, 1002, 1003, 1005, 1007, 1008, 1010, 1011,
	1013, 1014, 1016, 1017, 1019, 1021, 1022, 1024, 1025, 1027, 1028, 1030, 1031, 1033, 1034, 1036, 1038, 1039, 1041, 1042,
	1044, 1045, 1047, 1048, 1050, 1052, 1053, 1055, 1056, 1058, 1060, 1061, 1063, 1064, 1066, 1068, 1069, 1071, 1072, 1074,
	1076, 1077, 1079, 1080, 1082, 1084, 1085, 1087, 1088, 1090, 1092, 1093, 1095, 1096, 1098, 1100, 1101, 1103, 1104, 1106,
	1108, 1109, 1111, 1113, 1114, 1116, 1118, 1119, 1121, 1123, 1124, 1126, 1128, 1129, 1131, 1133, 1134, 1136, 1138, 1140,
	1141, 1143, 1145, 1146, 1148, 1150, 1151, 1153, 1155, 1156, 1158, 1160, 1161, 1163, 1165, 1166, 1168, 1170, 1172, 1173,
	1175, 1177, 1179, 1180, 1182, 1184, 1186, 1187, 1189, 1191, 1192, 1194, 1196, 1198, 1199, 1201, 1203, 1205, 1206, 1208,
	1210, 1212, 1213, 1215, 1217, 1219, 1220, 1222, 1224, 1225, 1227, 1229, 1231, 1232, 1234, 1236, 1238, 1239, 1241, 1243,
	1245, 1246, 1248, 1250, 1252, 1253, 1255, 1257, 1258, 1260, 1262, 1264, 1265, 1267, 1269, 1271, 1272, 1274, 1276, 1278,
	1279, 1281, 1283, 1285, 1286, 1288, 1290, 1292, 1293, 1295, 1297, 1299, 1301, 1303, 1304, 1306, 1308, 1310, 1312, 1314,
	1315, 1317, 1319, 1321, 1323, 1325, 1326, 1328, 1330, 1332, 1334, 1336, 1337, 1339, 1341, 1343, 1345, 1347, 1348, 1350,
	1352, 1354, 1356, 1358, 1360, 1362, 1364, 1365, 1367, 1369, 1371, 1373, 1375, 1377, 1379, 1381, 1382, 1384, 1386, 1388,
	1390, 1392, 1394, 1396, 1398, 1399, 1401, 1403, 1405, 1407, 1409, 1411, 1413, 1415, 1416, 1418, 1420, 1422, 1424, 1426,
	1428, 1430, 1432, 1433, 1435, 1437, 1439, 1441, 1443, 1445, 1447, 1449, 1450, 1452, 1454, 1456, 1458, 1460, 1462, 1464,
	1466, 1467, 1469, 1471, 1473, 1475, 1477, 1479, 1481, 1483, 1484, 1486, 1488, 1490, 1492, 1494, 1496, 1498, 1500, 1501,
	1503, 1505, 1507, 1509, 1511, 1513, 1515, 1517, 1518, 1520, 1522, 1524, 1526, 1528, 1530, 1532, 1534, 1535, 1537, 1539,
	1541, 1543, 1545, 1547, 1549, 1551, 1553, 1555, 1557, 1559, 1561, 1563, 1565, 1567, 1569, 1571, 1573, 1575, 1577, 1579,
	1581, 1583, 1585, 1587, 1589, 1591, 1593, 1595, 1597, 1599, 1601, 1603, 1605, 1607, 1609, 1611, 1613, 1615, 1617, 1619,
	1621, 1623, 1625, 1627, 1629, 1631, 1633, 1635, 1637, 1639, 1641, 1643, 1645, 1647, 1649, 1651, 1653, 1655, 1657, 1659,
	1661, 1663, 1665, 1667, 1669, 1672, 1674, 1676, 1678, 1680, 1682, 1684, 1686, 1688, 1690, 1692, 1694, 1696, 1698, 1700,
	1702, 1704, 1707, 1709, 1711, 1713, 1715, 1717, 1719, 1721, 1723, 1725, 1727, 1729, 1731, 1734, 1736, 1738, 1740, 1742,
	1745, 1747, 1749, 1751, 1753, 1756, 1758, 1760, 1762, 1764, 1766, 1769, 1771, 1773, 1775, 1777, 1780, 1782, 1784, 1786,
	1788, 1791, 1793, 1795, 1797, 1799, 1801, 1804, 1806, 1808, 1810, 1812, 1815, 1817, 1819, 1821, 1823, 1826, 1828, 1830,
	1832, 1834, 1836, 1839, 1841, 1843, 1845, 1847, 1850, 1852, 1854, 1856, 1858, 1860, 1863, 1865, 1867, 1869, 1871, 1873,
	1875, 1878, 1880, 1882, 1884, 1885, 1886, 1887, 1888, 1889, 1890, 1891, 1892, 1893, 1894, 1895, 1896, 1897, 1898, 1899,
	1900, 1901, 1902, 1903, 1904, 1905, 1906, 1907, 1908, 1909, 1910, 1911, 1912, 1913, 1914, 1915, 1916, 1917, 1918, 1919,
	1920, 1921, 1922, 1923, 1924, 1925, 1926, 1927, 1928, 1929, 1930, 1931, 1932, 1933, 1934, 1935, 1936, 1937, 1938, 1939,
	1940, 1941, 1942, 1943, 1944, 1945, 1946, 1947, 1948, 1949, 1950, 1951, 1952, 1953, 1954, 1955, 1956, 1957, 1958, 1959,
	1960, 1961, 1962, 1963, 1964, 1965, 1966, 1967, 1968, 1969, 1970, 1971, 1972, 1973, 1974, 1975, 1976, 1977, 1978, 1979,
	1980, 1981, 1982, 1983, 1984, 1985, 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
	2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019,
	2020, 2021, 2022, 2023, 2024, 2025, 2026, 2027, 2028, 2029, 2030, 2031, 2032, 2033, 2034, 2035, 2036, 2037, 2038, 2039,
	2040, 2041, 2042, 2043, 2044, 2045, 2046, 2047
};

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx, 0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);

	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static int ktz8868_set_brightness_level(unsigned int bl_lvl)
{
	if (bl_lvl > 0) {
		ktz8868_write_byte(0x04, bl_lvl & 0x07);
		ktz8868_write_byte(0x05, (bl_lvl >> 3) & 0xFF);
		if (ktz8868_set_bl_flag == false) {
			mdelay(15);
			ktz8868_write_byte(0x01, 0x01);
			ktz8868_set_bl_flag = true;
		}
	}

	if (bl_lvl == 0) {
		ktz8868_write_byte(0x01, 0x00);
		mdelay(9);
		ktz8868_write_byte(0x04, 0x00);
		ktz8868_write_byte(0x05, 0x00);
		if(ktz8868_set_bl_flag == true) {
			ktz8868_set_bl_flag = false;
		}
	}
	pr_info("[lcd_info]%s: bl_lvl=%d\n", __func__, bl_lvl);

	return 0;
}

static int ktz8868_backlight_config(struct drm_panel *panel, bool enable)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (enable) {
		if (ctx->bl_mode == LED_MODE_BLS_VIRTUAL) {
			ktz8868_write_byte(0x02, 0xD2); //i2c mode 34v
		} else if (ctx->bl_mode == LED_MODE_BLS_CABC) {
			ktz8868_write_byte(0x02, 0xD3); //i2c & pwm mode 34v
		}
		ktz8868_write_byte(0x03, 0xEB);
		ktz8868_write_byte(0x11, 0x76);
		ktz8868_write_byte(0x15, 0x88);
		ktz8868_write_byte(0x08, 0xFF);
	} else {
		ktz8868_write_byte(0x08, 0x00);
	}

	pr_info("[lcd_info]%s: enable=%d bl_mode =%d\n", __func__, enable, ctx->bl_mode);

	return 0;
}


/* ktz8868 lcd base config */
static void ktz8868_lcd_bias_config(struct drm_panel *panel, int enable)
{
	struct lcm *ctx = panel_to_lcm(panel);

	printk("[lcd_info]%s: ++ enable=%d\n", __func__, enable);
	ctx->bias_enp_en = devm_gpiod_get(ctx->dev, "bias-enp-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_enp_en)) {
		printk("[lcd_info][error]%s: could not get bias_enp_en gpio line=%d\n", __func__, __LINE__);
		return;
	}

	ctx->bias_enn_en = devm_gpiod_get(ctx->dev, "bias-enn-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_enn_en)) {
		printk("[lcd_info][error]%s: could not get bias_enn_en gpio line=%d\n", __func__, __LINE__);
		return;
	}

	if(!enable){
		/* Disable ENN */
		ktz8868_write_byte(0x09, 0x9C);
		printk("[lcd_info]%s: bias_enn_en 0\n", __func__);
		usleep_range(5000, 5010);
		/* Disable ENP */
		ktz8868_write_byte(0x09, 0x98);
		printk("[lcd_info]%s: bias_enp_en 0\n", __func__);
	} else {
		/* only config i2c0*/
		/* LCD_BOOST_CFG */
		ktz8868_write_byte(0x0C, 0x32);
		/* OUTP_CFG，OUTP = 6.0V */
		ktz8868_write_byte(0x0D, 0x28);
		/* OUTN_CFG，OUTN = -6.0V */
		ktz8868_write_byte(0x0E, 0x28);
		/* enable OUTN and OUTP via I2C Ctrl */
		ktz8868_write_byte(0x09, 0x98);
		/* enable ENP */
		ktz8868_write_byte(0x09, 0x9C);
		printk("[lcd_info]%s: bias_enp_en 1\n", __func__);
		usleep_range(5000, 5010);
		/* enable ENN */
		ktz8868_write_byte(0x09, 0x9E);
		usleep_range(10 * 1000, 15 * 1000); /* 10ms */
		printk("[lcd_info]%s: bias_enn_en 1\n", __func__);
	}
	devm_gpiod_put(ctx->dev, ctx->bias_enp_en);
	devm_gpiod_put(ctx->dev, ctx->bias_enn_en);

	printk("[lcd_info]%s: --\n", __func__);
}

/* VDDI Ctrl */
static int lcm_enable_vddi(struct drm_panel *panel, int enable)
{
	struct lcm *ctx = panel_to_lcm(panel);

	printk("[lcd_info]%s: ++\n", __func__);
	#if 0
	ctx->lcm_vddi_en = devm_gpiod_get(ctx->dev, "lcm-vddi-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->lcm_vddi_en)) {
		pr_err("[lcd_info][error]%s: could not get lcm_vddi_en gpio line=%d\n", __func__, __LINE__);
		return -1;
	}
	#endif
	if (enable) {
		gpiod_set_value(ctx->lcm_vddi_en, 1);
	} else {
			if (is_pd_with_guesture) {
				pr_info("[lcd_info]%s: vddi tp guesture enable, not disable backlight ic\n", __func__);
				devm_gpiod_put(ctx->dev, ctx->bias_en);
				return 0;
			}
		gpiod_set_value(ctx->lcm_vddi_en, 0);
	}
	printk("[lcd_info]%s: --\n", __func__);
	return 0;
}

/* backlight ic is ktz8868 */
static int lcm_backlight_ic_config(struct drm_panel *panel, int enable)
{
	struct lcm *ctx = panel_to_lcm(panel);

	printk("[lcd_info]%s: ++\n", __func__);
	ctx->bias_en = devm_gpiod_get(ctx->dev, "pm-enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_en)) {
		pr_err("[lcd_info][error]%s: could not get pm-enable gpio line=%d\n", __func__, __LINE__);
		return -1;
	}

	if (enable) {
		gpiod_set_value(ctx->bias_en, 1);
		usleep_range(125, 130);

		if (!is_pd_with_guesture) {
			/* lcd bias config enable  */
			ktz8868_lcd_bias_config(panel, true);
		}
		/* lcd brightness config enable*/
		ktz8868_backlight_config(panel, true);

	} else {
			if (is_pd_with_guesture) {
				pr_info("[lcd_info]%s: tp guesture enable, not disable backlight ic\n", __func__);
				ktz8868_backlight_config(panel, false);
				devm_gpiod_put(ctx->dev, ctx->bias_en);
				return 0;
			}
		ktz8868_lcd_bias_config(panel, false);
		ktz8868_backlight_config(panel, false);
		gpiod_set_value(ctx->bias_en, 0);
		msleep(20);
		/* if ktz8868 will shutdown, we shoudle set bl flag to false */
		ktz8868_set_bl_flag = false;
	}

	devm_gpiod_put(ctx->dev, ctx->bias_en);

	printk("[lcd_info]%s: --\n", __func__);
	return 0;
}
/*
int oplus25682_i2c_set_backlight(unsigned int level)
{
	if (level > MAX_NORMAL_BRIGHTNESS)
		level = MAX_NORMAL_BRIGHTNESS;

	level_backup = level;
	oplus_display_brightness = level;
	printk("[lcd_info]%s: bl_level:%d, mapping value = %d\n", __func__, level, backlight_map[level]);

	level = backlight_map[level];
	ktz8868_set_brightness_level(level);

	return 0;
}
EXPORT_SYMBOL(oplus25682_i2c_set_backlight);
*/

/* rc_buf_thresh will right shift 6bits (which means the values here will be divided by 64)
 * when setting to PPS8~PPS11 registers in mtk_dsc_config() function, so the original values
 * need left sihft 6bit (which means the original values are multiplied by 64), so that
 * PPS8~PPS11 registers can get right setting
 */
static unsigned int rc_buf_thresh[14] = {
//The original values VS values multiplied by 64
//14, 28,  42,	 56,   70,	 84,   98,	 105,  112,  119,  121,  123,  125,  126
896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
static unsigned int range_min_qp[15] = {0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12};
static unsigned int range_max_qp[15] = {4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13};
static int range_bpg_ofs[15] = {2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};

static void lcm_panel_init(struct lcm *ctx)
{
	pr_info("%s +\n", __func__);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (is_pd_with_guesture) {
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(5 * 1000, 10 * 1000);
		gpiod_set_value(ctx->reset_gpio, 1);
		usleep_range(10 * 1000, 15 * 1000);
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(10 * 1000, 15 * 1000);
		gpiod_set_value(ctx->reset_gpio, 1);
		usleep_range(100 * 1000, 110 * 1000);
	} else {
		gpiod_set_value(ctx->reset_gpio, 1);
		usleep_range(10 * 1000, 15 * 1000);
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(10 * 1000, 15 * 1000);
		gpiod_set_value(ctx->reset_gpio, 1);
		usleep_range(100 * 1000, 110 * 1000);
	}
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	//CABC start
	lcm_dcs_write_seq_static(ctx,0xFF,0x23);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0x00,0x60);
	lcm_dcs_write_seq_static(ctx,0x07,0x20);
	lcm_dcs_write_seq_static(ctx,0x08,0x01);
	lcm_dcs_write_seq_static(ctx,0x09,0x68);
	lcm_dcs_write_seq_static(ctx,0x11,0x03);
	lcm_dcs_write_seq_static(ctx,0x12,0x6B);
	lcm_dcs_write_seq_static(ctx,0x13,0x00);
	lcm_dcs_write_seq_static(ctx,0x15,0x03);
	lcm_dcs_write_seq_static(ctx,0x16,0x10);
	lcm_dcs_write_seq_static(ctx,0x0A,0xAA);
	lcm_dcs_write_seq_static(ctx,0x0B,0xAA);
	lcm_dcs_write_seq_static(ctx,0x0C,0xD5);
	lcm_dcs_write_seq_static(ctx,0x0D,0x05);
	lcm_dcs_write_seq_static(ctx,0x19,0x18);
	lcm_dcs_write_seq_static(ctx,0x1A,0x18);
	lcm_dcs_write_seq_static(ctx,0x1B,0x18);
	lcm_dcs_write_seq_static(ctx,0x1C,0x18);
	lcm_dcs_write_seq_static(ctx,0x1D,0x1A);
	lcm_dcs_write_seq_static(ctx,0x1E,0x1A);
	lcm_dcs_write_seq_static(ctx,0x1F,0x1B);
	lcm_dcs_write_seq_static(ctx,0x20,0x1D);
	lcm_dcs_write_seq_static(ctx,0x21,0x1E);
	lcm_dcs_write_seq_static(ctx,0x22,0x21);
	lcm_dcs_write_seq_static(ctx,0x23,0x27);
	lcm_dcs_write_seq_static(ctx,0x24,0x2B);
	lcm_dcs_write_seq_static(ctx,0x25,0x2F);
	lcm_dcs_write_seq_static(ctx,0x26,0x34);
	lcm_dcs_write_seq_static(ctx,0x27,0x39);
	lcm_dcs_write_seq_static(ctx,0x28,0x3E);
	lcm_dcs_write_seq_static(ctx,0x2A,0x32);
	lcm_dcs_write_seq_static(ctx,0x2B,0x3F);

	//UI MODE 55=01
	lcm_dcs_write_seq_static(ctx,0x30,0xFA);
	lcm_dcs_write_seq_static(ctx,0x31,0xF7);
	lcm_dcs_write_seq_static(ctx,0x32,0xF5);
	lcm_dcs_write_seq_static(ctx,0x33,0xF2);
	lcm_dcs_write_seq_static(ctx,0x34,0xF0);
	lcm_dcs_write_seq_static(ctx,0x35,0xED);
	lcm_dcs_write_seq_static(ctx,0x36,0xEB);
	lcm_dcs_write_seq_static(ctx,0x37,0xE8);
	lcm_dcs_write_seq_static(ctx,0x38,0xE6);
	lcm_dcs_write_seq_static(ctx,0x39,0xE2);
	lcm_dcs_write_seq_static(ctx,0x3A,0xDE);
	lcm_dcs_write_seq_static(ctx,0x3B,0xDB);
	lcm_dcs_write_seq_static(ctx,0x3D,0xD7);
	lcm_dcs_write_seq_static(ctx,0x3F,0xD3);
	lcm_dcs_write_seq_static(ctx,0x40,0xD0);
	lcm_dcs_write_seq_static(ctx,0x41,0xCC);

	//STILL MODE 55=02
	lcm_dcs_write_seq_static(ctx,0x45,0xF7);
	lcm_dcs_write_seq_static(ctx,0x46,0xEF);
	lcm_dcs_write_seq_static(ctx,0x47,0xE7);
	lcm_dcs_write_seq_static(ctx,0x48,0xDE);
	lcm_dcs_write_seq_static(ctx,0x49,0xD5);
	lcm_dcs_write_seq_static(ctx,0x4A,0xCD);
	lcm_dcs_write_seq_static(ctx,0x4B,0xC4);
	lcm_dcs_write_seq_static(ctx,0x4C,0xBB);
	lcm_dcs_write_seq_static(ctx,0x4D,0xB3);
	lcm_dcs_write_seq_static(ctx,0x4E,0xAF);
	lcm_dcs_write_seq_static(ctx,0x4F,0xAC);
	lcm_dcs_write_seq_static(ctx,0x50,0xA9);
	lcm_dcs_write_seq_static(ctx,0x51,0xA5);
	lcm_dcs_write_seq_static(ctx,0x52,0xA2);
	lcm_dcs_write_seq_static(ctx,0x53,0x9F);
	lcm_dcs_write_seq_static(ctx,0x54,0x9C);

	//MOVING MODE 55=03
	lcm_dcs_write_seq_static(ctx,0x58,0xF5);
	lcm_dcs_write_seq_static(ctx,0x59,0xEB);
	lcm_dcs_write_seq_static(ctx,0x5A,0xE1);
	lcm_dcs_write_seq_static(ctx,0x5B,0xD7);
	lcm_dcs_write_seq_static(ctx,0x5C,0xCC);
	lcm_dcs_write_seq_static(ctx,0x5D,0xC2);
	lcm_dcs_write_seq_static(ctx,0x5E,0xB8);
	lcm_dcs_write_seq_static(ctx,0x5F,0xAD);
	lcm_dcs_write_seq_static(ctx,0x60,0xA3);
	lcm_dcs_write_seq_static(ctx,0x61,0x9C);
	lcm_dcs_write_seq_static(ctx,0x62,0x92);
	lcm_dcs_write_seq_static(ctx,0x63,0x8B);
	lcm_dcs_write_seq_static(ctx,0x64,0x85);
	lcm_dcs_write_seq_static(ctx,0x65,0x7E);
	lcm_dcs_write_seq_static(ctx,0x66,0x77);
	lcm_dcs_write_seq_static(ctx,0x67,0x70);
	lcm_dcs_write_seq_static(ctx,0x6E,0x00);
	lcm_dcs_write_seq_static(ctx,0x6F,0x00);
	lcm_dcs_write_seq_static(ctx,0x70,0x00);
	lcm_dcs_write_seq_static(ctx,0x71,0x00);
	// CABC end


	lcm_dcs_write_seq_static(ctx,0xFF,0xE0);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0xCC,0x11);

	lcm_dcs_write_seq_static(ctx,0xFF,0xF0);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0x75,0x33,0x01,0x03);
	lcm_dcs_write_seq_static(ctx,0xE4,0x10);
	lcm_dcs_write_seq_static(ctx,0xFF,0x20);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x21,0x00,0x4E,0x00,0x6E,0x00,0x8C,0x00,0xA4,0x00,0xBC,0x00,0xCE);
	lcm_dcs_write_seq_static(ctx,0xB1,0x00,0xE1,0x01,0x1B,0x01,0x46,0x01,0x88,0x01,0xBB,0x02,0x08,0x02,0x44,0x02,0x45);
	lcm_dcs_write_seq_static(ctx,0xB2,0x02,0x7F,0x02,0xBC,0x02,0xE5,0x03,0x1C,0x03,0x3F,0x03,0x6A,0x03,0x79,0x03,0x88);
	lcm_dcs_write_seq_static(ctx,0xB3,0x03,0x99,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB4,0x00,0x00,0x00,0x20,0x00,0x4D,0x00,0x70,0x00,0x8C,0x00,0xA5,0x00,0xBA,0x00,0xCE);
	lcm_dcs_write_seq_static(ctx,0xB5,0x00,0xDF,0x01,0x18,0x01,0x44,0x01,0x86,0x01,0xB7,0x02,0x02,0x02,0x3D,0x02,0x3E);
	lcm_dcs_write_seq_static(ctx,0xB6,0x02,0x78,0x02,0xB7,0x02,0xE0,0x03,0x19,0x03,0x3C,0x03,0x67,0x03,0x79,0x03,0x88);
	lcm_dcs_write_seq_static(ctx,0xB7,0x03,0x99,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB8,0x00,0x00,0x00,0x21,0x00,0x4F,0x00,0x76,0x00,0x97,0x00,0xB2,0x00,0xC9,0x00,0xDE);
	lcm_dcs_write_seq_static(ctx,0xB9,0x00,0xF1,0x01,0x2A,0x01,0x55,0x01,0x96,0x01,0xC6,0x02,0x0F,0x02,0x49,0x02,0x4A);
	lcm_dcs_write_seq_static(ctx,0xBA,0x02,0x83,0x02,0xC0,0x02,0xE9,0x03,0x22,0x03,0x47,0x03,0x77,0x03,0x79,0x03,0x89);
	lcm_dcs_write_seq_static(ctx,0xBB,0x03,0x9A,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xC6,0x26);
	lcm_dcs_write_seq_static(ctx,0xC7,0x22);
	lcm_dcs_write_seq_static(ctx,0xC8,0x33);
	lcm_dcs_write_seq_static(ctx,0xC9,0x22);
	lcm_dcs_write_seq_static(ctx,0xCA,0x21);
	lcm_dcs_write_seq_static(ctx,0xCB,0x10);
	lcm_dcs_write_seq_static(ctx,0xCC,0x31);
	lcm_dcs_write_seq_static(ctx,0xCD,0x63);
	lcm_dcs_write_seq_static(ctx,0xCE,0xA2);
	lcm_dcs_write_seq_static(ctx,0xCF,0xB6);
	lcm_dcs_write_seq_static(ctx,0xD0,0xC4);
	lcm_dcs_write_seq_static(ctx,0xD1,0xE2);
	lcm_dcs_write_seq_static(ctx,0xD2,0x26);
	lcm_dcs_write_seq_static(ctx,0xD3,0x22);
	lcm_dcs_write_seq_static(ctx,0xD4,0x33);
	lcm_dcs_write_seq_static(ctx,0xD5,0x21);
	lcm_dcs_write_seq_static(ctx,0xD6,0x21);
	lcm_dcs_write_seq_static(ctx,0xD7,0x00);
	lcm_dcs_write_seq_static(ctx,0xD8,0x22);
	lcm_dcs_write_seq_static(ctx,0xD9,0x63);
	lcm_dcs_write_seq_static(ctx,0xDA,0x82);
	lcm_dcs_write_seq_static(ctx,0xDB,0xA7);
	lcm_dcs_write_seq_static(ctx,0xDC,0xA4);
	lcm_dcs_write_seq_static(ctx,0xDD,0xE2);
	lcm_dcs_write_seq_static(ctx,0xDE,0x26);
	lcm_dcs_write_seq_static(ctx,0xDF,0x22);
	lcm_dcs_write_seq_static(ctx,0xE0,0x33);
	lcm_dcs_write_seq_static(ctx,0xE1,0x22);
	lcm_dcs_write_seq_static(ctx,0xE2,0x21);
	lcm_dcs_write_seq_static(ctx,0xE3,0x00);
	lcm_dcs_write_seq_static(ctx,0xE4,0x22);
	lcm_dcs_write_seq_static(ctx,0xE5,0x63);
	lcm_dcs_write_seq_static(ctx,0xE6,0x82);
	lcm_dcs_write_seq_static(ctx,0xE7,0xA7);
	lcm_dcs_write_seq_static(ctx,0xE8,0xA4);
	lcm_dcs_write_seq_static(ctx,0xE9,0xE2);
	lcm_dcs_write_seq_static(ctx,0xFF,0x21);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0xB0,0x00,0x00,0x00,0x21,0x00,0x4E,0x00,0x6E,0x00,0x8C,0x00,0xA4,0x00,0xBC,0x00,0xCE);
	lcm_dcs_write_seq_static(ctx,0xB1,0x00,0xE1,0x01,0x1B,0x01,0x46,0x01,0x88,0x01,0xBB,0x02,0x08,0x02,0x44,0x02,0x45);
	lcm_dcs_write_seq_static(ctx,0xB2,0x02,0x7F,0x02,0xBC,0x02,0xE5,0x03,0x1C,0x03,0x3F,0x03,0x6A,0x03,0x79,0x03,0x88);
	lcm_dcs_write_seq_static(ctx,0xB3,0x03,0x99,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB4,0x00,0x00,0x00,0x20,0x00,0x4D,0x00,0x70,0x00,0x8C,0x00,0xA5,0x00,0xBA,0x00,0xCE);
	lcm_dcs_write_seq_static(ctx,0xB5,0x00,0xDF,0x01,0x18,0x01,0x44,0x01,0x86,0x01,0xB7,0x02,0x02,0x02,0x3D,0x02,0x3E);
	lcm_dcs_write_seq_static(ctx,0xB6,0x02,0x78,0x02,0xB7,0x02,0xE0,0x03,0x19,0x03,0x3C,0x03,0x67,0x03,0x79,0x03,0x88);
	lcm_dcs_write_seq_static(ctx,0xB7,0x03,0x99,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xB8,0x00,0x00,0x00,0x21,0x00,0x4F,0x00,0x76,0x00,0x97,0x00,0xB2,0x00,0xC9,0x00,0xDE);
	lcm_dcs_write_seq_static(ctx,0xB9,0x00,0xF1,0x01,0x2A,0x01,0x55,0x01,0x96,0x01,0xC6,0x02,0x0F,0x02,0x49,0x02,0x4A);
	lcm_dcs_write_seq_static(ctx,0xBA,0x02,0x83,0x02,0xC0,0x02,0xE9,0x03,0x22,0x03,0x47,0x03,0x77,0x03,0x79,0x03,0x89);
	lcm_dcs_write_seq_static(ctx,0xBB,0x03,0x9A,0x03,0xAB,0x03,0xC0,0x03,0xD6,0x03,0xE5,0x03,0xFF,0x00,0x00);
	lcm_dcs_write_seq_static(ctx,0xFF,0x10);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0x3B,0x03,0xE8,0x38,0x04,0x04,0x00);
	lcm_dcs_write_seq_static(ctx,0x90,0x13);
	lcm_dcs_write_seq_static(ctx,0x91,0x89,0xA8,0x00,0x14,0xD2,0x00,0x00,0x00,0x02,0x98,0x00,0x13,0x05,0x7A,0x01,0xF3);
	lcm_dcs_write_seq_static(ctx,0x92,0x10,0xE0);
	lcm_dcs_write_seq_static(ctx,0x9D,0x01);
	if(g_fps_current == 120 || g_fps_current == 60){
	    lcm_dcs_write_seq_static(ctx,0xB2,0x91);
	    lcm_dcs_write_seq_static(ctx,0xB3,0x00);
	}
	if(g_fps_current == 90 || g_fps_current == 48){
	    lcm_dcs_write_seq_static(ctx,0xB2,0x80);
	    lcm_dcs_write_seq_static(ctx,0xB3,0x40);
	}
	lcm_dcs_write_seq_static(ctx,0x35,0x00);

	lcm_dcs_write_seq_static(ctx,0xFF,0x27);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0xD0,0x31);
	lcm_dcs_write_seq_static(ctx,0xD1,0x54);
	lcm_dcs_write_seq_static(ctx,0xDE,0x42);
	lcm_dcs_write_seq_static(ctx,0xDF,0x02);

	lcm_dcs_write_seq_static(ctx,0xFF,0x10);
	lcm_dcs_write_seq_static(ctx,0xFB,0x01);
	lcm_dcs_write_seq_static(ctx,0x11);
	M_DELAY(120);
	lcm_dcs_write_seq_static(ctx,0x29);
	M_DELAY(10);
	if ((ctx->bl_mode == LED_MODE_BLS_CABC) && (level_backup > 0)) {
		pr_info("%s level_backup:%d\n", __func__, level_backup);
		lcm_dcs_write_seq(ctx, 0x51, ((level_backup >> 8) & 0x07), level_backup & 0xFF);
		lcm_dcs_write_seq(ctx, 0x53, 0x2C);
	}

	pr_info("%s -\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n", __func__);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	pr_info("%s---\n", __func__);

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	int blank = 0;
#endif

	pr_info("%s+++\n", __func__);

	if (!ctx->prepared)
		return 0;

#if IS_ENABLED(CONFIG_TOUCHPANEL_NOTIFY)
	if (tp_gesture_enable_notifier && tp_gesture_enable_notifier(0) && (g_shutdown == 0) && (esd_flag == 0)) {
		is_pd_with_guesture = true;
	} else {
		is_pd_with_guesture = false;
	}
#endif

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	if (is_pd_with_guesture) {
		pr_info("[TP] tp gesture is enable, Display not to poweroff\n");
	} else {
		blank = LCD_CTL_CS_OFF;
		mtk_disp_notifier_call_chain(MTK_DISP_EVENT_FOR_TOUCH, &blank);
		pr_info("[TP]TP CS will change to gpio mode and low\n");
	}
#endif

	lcm_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	usleep_range(5 * 1000, 10 * 1000);
	lcm_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	usleep_range(110 * 1000, 115 * 1000);

	if (is_pd_with_guesture) {

	} else {
		ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(5 * 1000, 5 * 1000);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	}

	ret = lcm_backlight_ic_config(panel, 0);
	if(ret) {
		printk("[lcd_info][error]%s: set bl_bias disable failed! ret=%d line=%d\n", __func__, ret, __LINE__);
	}

	if (!IS_ERR_OR_NULL(ctx->pmic_regmap))
		regmap_set_bits(ctx->pmic_regmap, MT6363_BUCK_VS1_VOTER_CON1_CLR, LCM_LDO_KEEP_AWAKE_BIT);

	ret = lcm_enable_vddi(panel, 0);
	if(ret) {
		pr_err("[lcd_info][error]%s: set vddio off failed! ret=%d line=%d\n", __func__, ret, __LINE__);
	}

	ctx->error = 0;
	ctx->prepared = false;

	pr_info("%s---\n", __func__);

	return 0;
}
static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;
#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	int blank = 0;
#endif

	pr_info("%s+++\n", __func__);

	if (ctx->prepared)
		return 0;

	if (!IS_ERR_OR_NULL(ctx->pmic_regmap))
		regmap_set_bits(ctx->pmic_regmap, MT6363_BUCK_VS1_VOTER_CON1_SET, LCM_LDO_KEEP_AWAKE_BIT);

	ret = lcm_backlight_ic_config(panel, 1);
	if(ret) {
		printk("[lcd_info][error]%s: set bl_bias enable failed! ret=%d line=%d\n", __func__, ret, __LINE__);
	}

	lcm_panel_init(ctx);
	ret = ctx->error;
	if (ret < 0) {
		pr_info("Send initial code error!\n");
		lcm_unprepare(panel);
	}

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

#if IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY)
	blank = LCD_CTL_CS_ON;
	mtk_disp_notifier_call_chain(MTK_DISP_EVENT_FOR_TOUCH, &blank);
	pr_info("[TP] spi CS set to high\n");
#endif
	pr_info("%s---\n", __func__);

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_info("%s+++\n", __func__);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	pr_info("%s---\n", __func__);

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 794576, //2988*2224*120 htotal*vtotal*fps
	.hdisplay = HAC_FHD,
	.hsync_start = HAC_FHD + HFP_120_60HZ,
	.hsync_end = HAC_FHD + HFP_120_60HZ + HSA,
	.htotal = HAC_FHD + HFP_120_60HZ + HSA + HBP_120_60HZ,//2924
	.vdisplay = VAC_FHD,
	.vsync_start = VAC_FHD + VFP_120HZ,
	.vsync_end = VAC_FHD + VFP_120HZ + VSA,
	.vtotal = VAC_FHD + VFP_120HZ + VSA + VBP, //4576
	.hskew = 1,
};

static const struct drm_display_mode performance_mode_90hz = {
	.clock = 634239, //2924*2288*90 htotal*vtotal*fps
	.hdisplay = HAC_FHD,
	.hsync_start = HAC_FHD + HFP_90_48HZ,
	.hsync_end = HAC_FHD + HFP_90_48HZ + HSA,
	.htotal = HAC_FHD + HFP_90_48HZ + HSA + HBP_90_48HZ,//2988
	.vdisplay = VAC_FHD,
	.vsync_start = VAC_FHD + VFP_90HZ,
	.vsync_end = VAC_FHD + VFP_90HZ + VSA,
	.vtotal = VAC_FHD + VFP_90HZ + VSA + VBP, //2224
	.hskew = 1,
};

static const struct drm_display_mode performance_mode_60hz = {
	.clock = 794576, //2988*2224*90 htotal*vtotal*fps
	.hdisplay = HAC_FHD,
	.hsync_start = HAC_FHD + HFP_120_60HZ,
	.hsync_end = HAC_FHD + HFP_120_60HZ + HSA,
	.htotal = HAC_FHD + HFP_120_60HZ + HSA + HBP_120_60HZ,//2988
	.vdisplay = VAC_FHD,
	.vsync_start = VAC_FHD + VFP_60HZ,
	.vsync_end = VAC_FHD + VFP_60HZ + VSA,
	.vtotal = VAC_FHD + VFP_60HZ + VSA + VBP, //2224
	.hskew = 1,
};

static const struct drm_display_mode performance_mode_48hz = {
	.clock = 634239, //2924*2288*90 htotal*vtotal*fps
	.hdisplay = HAC_FHD,
	.hsync_start = HAC_FHD + HFP_90_48HZ,
	.hsync_end = HAC_FHD + HFP_90_48HZ + HSA,
	.htotal = HAC_FHD + HFP_90_48HZ + HSA + HBP_90_48HZ,//2988
	.vdisplay = VAC_FHD,
	.vsync_start = VAC_FHD + VFP_48HZ,
	.vsync_end = VAC_FHD + VFP_48HZ + VSA,
	.vtotal = VAC_FHD + VFP_48HZ + VSA + VBP, //2224
	.hskew = 1,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 463,
	.data_rate = 926,
	.data_rate_khz = 926924,
	.physical_width_um = 278020,
	.physical_height_um = 179060,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.lcm_esd_check_table[0] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x00,
	},
	.dsc_params = {
		.enable = 1,
		.ver = 0x12, /* [7:4] major [3:0] minor */
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1400,
		.slice_height = 20,
		.slice_width = 1400,
		.chunk_size = 1400,
		.xmit_delay = 512,
		.dec_delay = 1015,
		.scale_value = 32,
		.increment_interval = 664,
		.decrement_interval = 19,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 499,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
 	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.dfps_cmd_table[0] = {0, 2 , {0xFF, 0x10}},
		.dfps_cmd_table[1] = {0, 2,  {0xFB, 0x01}},
		.dfps_cmd_table[2] = {0, 2 , {0xB2, 0x91}},
		.dfps_cmd_table[3] = {0, 2 , {0xB3, 0x00}},
	},
	.dyn = {
		.switch_en = 1,
		.hfp = HFP_120_60HZ,
		.vfp = VFP_120HZ,
		.hbp = HBP_120_60HZ,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 463,
	.data_rate = 926,
	.data_rate_khz = 926924,
	.physical_width_um = 278020,
	.physical_height_um = 179060,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x00,
	},
	.dsc_params = {
		.enable = 1,
		.ver = 0x12, /* [7:4] major [3:0] minor */
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1400,
		.slice_height = 20,
		.slice_width = 1400,
		.chunk_size = 1400,
		.xmit_delay = 512,
		.dec_delay = 1015,
		.scale_value = 32,
		.increment_interval = 664,
		.decrement_interval = 19,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 499,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 90,
		.dfps_cmd_table[0] = {0, 2 , {0xFF, 0x10}},
		.dfps_cmd_table[1] = {0, 2,  {0xFB, 0x01}},
		.dfps_cmd_table[2] = {0, 2 , {0xB2, 0x80}},
		.dfps_cmd_table[3] = {0, 2 , {0xB3, 0x40}},
	},
	.dyn = {
		.switch_en = 1,
		.hfp = HFP_90_48HZ,
		.vfp = VFP_90HZ,
		.hbp = HBP_90_48HZ,
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.pll_clk = 463,
	.data_rate = 926,
	.data_rate_khz = 926924,
	.physical_width_um = 278020,
	.physical_height_um = 179060,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x00,
	},
	.dsc_params = {
		.enable = 1,
		.ver = 0x12, /* [7:4] major [3:0] minor */
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1400,
		.slice_height = 20,
		.slice_width = 1400,
		.chunk_size = 1400,
		.xmit_delay = 512,
		.dec_delay = 1015,
		.scale_value = 32,
		.increment_interval = 664,
		.decrement_interval = 19,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 499,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 60,
		.dfps_cmd_table[0] = {0, 2 , {0xFF, 0x10}},
		.dfps_cmd_table[1] = {0, 2,  {0xFB, 0x01}},
		.dfps_cmd_table[2] = {0, 2 , {0xB2, 0x91}},
		.dfps_cmd_table[3] = {0, 2 , {0xB3, 0x00}},
	},
	.dyn = {
		.switch_en = 1,
		.hfp = HFP_120_60HZ,
		.vfp = VFP_60HZ,
		.hbp = HBP_120_60HZ,
	},
};

static struct mtk_panel_params ext_params_48hz = {
	.pll_clk = 463,
	.data_rate = 926,
	.data_rate_khz = 926924,
	.physical_width_um = 278020,
	.physical_height_um = 179060,
	.output_mode = MTK_PANEL_DUAL_PORT,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.lcm_cmd_if = MTK_PANEL_DUAL_PORT,
	.dual_swap = false,
	.vdo_per_frame_lp_enable = 1,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x53,
		.count = 1,
		.para_list[0] = 0x00,
	},
	.dsc_params = {
		.enable = 1,
		.ver = 0x12, /* [7:4] major [3:0] minor */
		.slice_mode = 0,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2000,
		.pic_width = 1400,
		.slice_height = 20,
		.slice_width = 1400,
		.chunk_size = 1400,
		.xmit_delay = 512,
		.dec_delay = 1015,
		.scale_value = 32,
		.increment_interval = 664,
		.decrement_interval = 19,
		.line_bpg_offset = 13,
		.nfl_bpg_offset = 1402,
		.slice_bpg_offset = 499,
		.initial_offset = 6144,
		.final_offset = 4320,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,

		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 48,
		.dfps_cmd_table[0] = {0, 2 , {0xFF, 0x10}},
		.dfps_cmd_table[1] = {0, 2,  {0xFB, 0x01}},
		.dfps_cmd_table[2] = {0, 2 , {0xB2, 0x80}},
		.dfps_cmd_table[3] = {0, 2 , {0xB3, 0x40}},
	},
	.dyn = {
		.switch_en = 1,
		.hfp = HFP_90_48HZ,
		.vfp = VFP_48HZ,
		.hbp = HBP_90_48HZ,
	},
};

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int lcm_panel_poweron(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret = 0;

	pr_info("[lcd_info]%s: ++\n", __func__);

	if (ctx->prepared){
		pr_info("[lcd_info]%s: frist time ctx->prepared=%d\n", __func__, ctx->prepared);
		return 0;
	}

	//set vddi 1.8v
	ret = lcm_enable_vddi(panel, 1);
	if(ret) {
		pr_err("[lcd_info][error]%s: set vddio on failed! ret=%d line=%d\n", __func__, ret, __LINE__);
	}
	M_DELAY(10);

	pr_info("[lcd_info]%s: --\n", __func__);
	return ret;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	unsigned int bl_level = 0;
	unsigned char cabc_brightness_lvl[] = {0x51, 0x07, 0xff};
	unsigned char cabc_open[] = {0x53, 0x2C};
	unsigned char cabc_close[] = {0x53, 0x00};

	if (level > MAX_HW_BRIGHTNESS)
		bl_level = MAX_HW_BRIGHTNESS;
	else
		bl_level = level;

	level_backup = bl_level;
	oplus_display_brightness = bl_level;
	pr_info("[lcd_info]%s: bl_level:%d, mapping value = %d\n", __func__, bl_level, backlight_map[bl_level]);

	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT && bl_level > 0) {
		bl_level = 1024;
	}

	cabc_brightness_lvl[1] = (backlight_map[bl_level] >> 8) & 0x07;
	cabc_brightness_lvl[2] = backlight_map[bl_level] & 0xFF;

	if (!cb)
		return -1;

	/*I2C & PWM mode set i2c level to max firstly,Only ctrl by pwm*/
	if (ktz8868_set_bl_flag == false) {
		ktz8868_set_brightness_level(MAX_HW_BRIGHTNESS);
	}

	pr_info("[lcd_info]%s level = %d,backlight = %d,cabc_brightness_lvl[1] = 0x%x,cabc_brightness_lvl[2] = 0x%x\n",
		__func__, level, bl_level, cabc_brightness_lvl[1], cabc_brightness_lvl[2]);

	cb(dsi, handle, cabc_brightness_lvl, ARRAY_SIZE(cabc_brightness_lvl));
	if(bl_level == 0) {
		cb(dsi, handle, cabc_close, ARRAY_SIZE(cabc_close));
		ktz8868_set_brightness_level(0);
		usleep_range(1 * 1000, 2 * 1000);
	} else {
		cb(dsi, handle, cabc_open, ARRAY_SIZE(cabc_open));
	}

	return 0;
}

#define CABC_MODE_CMD_SIZE 5
static void cabc_mode_switch(void *dsi, dcs_write_gce cb,
		void *handle, unsigned int cabc_mode)
{
	unsigned char cabc_mode_para = 0;
	int i = 0;
	unsigned char cabc_mode_cmd[CABC_MODE_CMD_SIZE][2] = {
		{0xFF, 0x10},
		{0xFB, 0x01},
		{0xB9, 0x00},
		{0x55, 0x00},
		{0xB9, 0x02},
	};

	if (cabc_mode == 0) {
		cabc_mode_para = 0;
	} else if (cabc_mode == 1) {
		cabc_mode_para = 1;
	} else if (cabc_mode == 2) {
		cabc_mode_para = 2;
	} else if (cabc_mode == 3) {
		cabc_mode_para = 3;
	} else {
		pr_info("[lcd_info]%s: cabc_mode=%d is not support, close cabc !\n", __func__, cabc_mode);
		cabc_mode_para = 0;
	}

	cabc_mode_cmd[3][1] = cabc_mode_para;
	for (i = 0; i < CABC_MODE_CMD_SIZE; i++) {
		cb(dsi, handle, cabc_mode_cmd[i], ARRAY_SIZE(cabc_mode_cmd[i]));
	}

	pr_info("[lcd_info]%s:cabc mode_%d, set cabc_para=%d\n", __func__, cabc_mode, cabc_mode_para);
}

struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	int target_fps = 0;
	struct drm_display_mode *m = get_mode_by_id(connector, mode);

	target_fps = drm_mode_vrefresh(m);


	if (target_fps == 120)
		ext->params = &ext_params;
	else if (target_fps == 90)
		ext->params = &ext_params_90hz;
	else if (target_fps == 60)
		ext->params = &ext_params_60hz;
	else if (target_fps == 48)
		ext->params = &ext_params_48hz;
	else
		ret = 1;

    g_fps_current = target_fps;
	return ret;
}

static int lcd_esd_gpio_read(struct drm_panel *panel)
{
	struct lcm *ctx = container_of(panel, struct lcm, panel);
	int master_read_value = 0, slave_read_value = 0;
	int ret = 0;

	master_read_value = gpiod_get_value(ctx->master_esd_gpio);
	slave_read_value = gpiod_get_value(ctx->slave_esd_gpio);
	printk("[lcd_info][ESD]%s: master:%d slave:%d\n", __func__, master_read_value, slave_read_value);
	if( master_read_value || slave_read_value) {

		pr_err("[ESD]%s: triger esd to recovery\n", __func__);
		ret = 1;

	} else {
		ret = 0;
	}

	if (1 == ret) {
		char payload[200] = "";
		int cnt = 0;

		cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "DisplayDriverID@@507$$");
		cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "ESD:");
		cnt += scnprintf(payload + cnt, sizeof(payload) - cnt, "master_0x%x, slave_0x%x",
			master_read_value, slave_read_value);
		pr_err("ESD check failed: %s\n", payload);
		mm_fb_display_kevent(payload, MM_FB_KEY_RATELIMIT_1H, "ESD check failed");
	}

	printk("[lcd_info][ESD]%s:ret=%d\n", __func__, ret);
	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.panel_poweron = lcm_panel_poweron,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.cabc_switch = cabc_mode_switch,
	.esd_read_gpio = lcd_esd_gpio_read,
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;
	struct drm_display_mode *mode3;
	struct drm_display_mode *mode4;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		pr_info("failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_90hz.hdisplay, performance_mode_90hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_90hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	mode3 = drm_mode_duplicate(connector->dev, &performance_mode_60hz);
	if (!mode3) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_60hz.hdisplay, performance_mode_60hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_60hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode3);

	mode4 = drm_mode_duplicate(connector->dev, &performance_mode_48hz);
	if (!mode4) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode_48hz.hdisplay, performance_mode_48hz.vdisplay,
			 drm_mode_vrefresh(&performance_mode_48hz));
		return -ENOMEM;
	}
	drm_mode_set_name(mode4);
	mode4->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode4);

	connector->display_info.width_mm = 273;
	connector->display_info.height_mm = 171;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct device_node *led_node;
	struct lcm *ctx;
	struct device_node *pmic_np;
	struct platform_device *pmic_pdev;

	int ret;

	pr_info("%s+++\n", __func__);

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}
	pr_info("It's oplus25682_nt36532c_2800_2000_dual_dsi_vdo_120hz_csot\n");

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	pmic_np = of_find_compatible_node(NULL, NULL, "mediatek,mt6363-pinctrl");
	if (pmic_np) {
		pmic_pdev = of_find_device_by_node(pmic_np);
		of_node_put(pmic_np);
		if (!pmic_pdev) {
			pr_err("Failed to get PMIC pdev\n");
			return -EPROBE_DEFER;
		} else {
			ctx->pmic_regmap = dev_get_regmap(pmic_pdev->dev.parent, NULL);
			if (IS_ERR_OR_NULL(ctx->pmic_regmap))
				pr_err("Failed to get PMIC regmap\n");
			else
				regmap_set_bits(ctx->pmic_regmap, MT6363_BUCK_VS1_VOTER_CON1_SET,
					LCM_LDO_KEEP_AWAKE_BIT);
			platform_device_put(pmic_pdev);
		}
	}

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	led_node = of_find_node_by_name(NULL, "mtk-leds");
	if (led_node) {
		const char *comp_str = NULL;
		of_property_read_string(led_node, "compatible", &comp_str);
		pr_info("Found mtk_leds node: %s\n", comp_str ? comp_str : "N/A");

		if (of_device_is_compatible(led_node, "mediatek,mtk-leds")) {
			ctx->bl_mode = LED_MODE_BLS_VIRTUAL;
			pr_info("Backlight mode set to VIRTUAL\n");
		} else if (of_device_is_compatible(led_node, "mediatek,disp-leds")) {
			ctx->bl_mode = LED_MODE_BLS_CABC;
			pr_info("Backlight mode set to CABC\n");
		} else {
			ctx->bl_mode = LED_MODE_BLS_NONE;
			pr_info("Unknown backlight mode: %s\n", comp_str);
		}
		of_node_put(led_node);
	} else {
		ctx->bl_mode = LED_MODE_BLS_NONE;
		pr_info("Failed to find mtk_leds node\n");
	}

	ctx->display_dual_swap = of_property_read_bool(dev->of_node,
					      "display-dual-swap");
	pr_notice("ctx->display_dual_swap=%d\n", ctx->display_dual_swap);
	if (ctx->display_dual_swap)
		ext_params.dual_swap = true;

	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		printk("[lcd_info][error]%s: cannot get reset-gpios %ld\n",
			 __func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	ctx->bias_en = devm_gpiod_get(ctx->dev, "pm-enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_en)) {
		printk("[lcd_info][error]%s: cannot get bias_en %ld\n",
			 __func__, PTR_ERR(ctx->bias_en));
		return PTR_ERR(ctx->bias_en);
	}
	gpiod_set_value(ctx->bias_en, 1);
	printk("[lcd_info]%s: set BL_EN to high\n", __func__);
	devm_gpiod_put(ctx->dev, ctx->bias_en);

	ctx->lcm_vddi_en = devm_gpiod_get(ctx->dev, "lcm-vddi-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->lcm_vddi_en)) {
		printk("[lcd_info][error]%s: cannot getbias-enp-en-gpios %ld\n",
			 __func__, PTR_ERR(ctx->lcm_vddi_en));
		return PTR_ERR(ctx->lcm_vddi_en);
	}
	devm_gpiod_put(ctx->dev, ctx->lcm_vddi_en);

	ctx->bias_enp_en = devm_gpiod_get(ctx->dev, "bias-enp-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_enp_en)) {
		printk("[lcd_info][error]%s: cannot getbias-enp-en-gpios %ld\n",
			 __func__, PTR_ERR(ctx->bias_enp_en));
		return PTR_ERR(ctx->bias_enp_en);
	}
	devm_gpiod_put(ctx->dev, ctx->bias_enp_en);

	ctx->bias_enn_en = devm_gpiod_get(ctx->dev, "bias-enn-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_enn_en)) {
		printk("[lcd_info][error]%s: cannot getbias-enp-en-gpios %ld\n",
			 __func__, PTR_ERR(ctx->bias_enn_en));
		return PTR_ERR(ctx->bias_enn_en);
	}
	devm_gpiod_put(ctx->dev, ctx->bias_enn_en);

	printk("[lcd_info][ESD]%s master_esd_gpio line=%d\n", __func__, __LINE__);
	ctx->master_esd_gpio = devm_gpiod_get_optional(ctx->dev, "master-esd", GPIOD_IN);
	if (IS_ERR(ctx->master_esd_gpio)) {
		printk("[lcd_info][error]%s: cannot get master_esd_gpio %ld\n",
			__func__, PTR_ERR(ctx->master_esd_gpio));
		return PTR_ERR(ctx->master_esd_gpio);
	} else {
		gpiod_direction_input(ctx->master_esd_gpio);
	}

	printk("[lcd_info][ESD]%s: slave_esd_gpio line=%d\n", __func__, __LINE__);
	ctx->slave_esd_gpio = devm_gpiod_get_optional(ctx->dev, "slave-esd", GPIOD_IN);
	if (IS_ERR(ctx->slave_esd_gpio)) {
		printk("[lcd_info][error]%s: cannot get slave_esd_gpio %ld\n",
			__func__, PTR_ERR(ctx->slave_esd_gpio));
		return PTR_ERR(ctx->slave_esd_gpio);
	} else {
		gpiod_direction_input(ctx->slave_esd_gpio);
	}

	ctx->prepared = true;
	ctx->enabled = true;
	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);

	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		dev_err(dev, "mipi_dsi_attach fail, ret=%d\n", ret);
		return -EPROBE_DEFER;
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif
	oplus_max_normal_brightness = MAX_NORMAL_BRIGHTNESS;

	/* wanhang */
	register_device_proc("lcd", "nt36532c", "csot");
	pr_info("%s-\n", __func__);

	return ret;
}
static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	if (ext_ctx != NULL) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
#endif

	if (!IS_ERR_OR_NULL(ctx->pmic_regmap))
		regmap_set_bits(ctx->pmic_regmap, MT6363_BUCK_VS1_VOTER_CON1_CLR, LCM_LDO_KEEP_AWAKE_BIT);
}
static const struct of_device_id lcm_of_match[] = {
	{
		.compatible = "ae150_p_d_a0038_vdo_evt",
	},
	{}
};
MODULE_DEVICE_TABLE(of, lcm_of_match);
static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
			.name = "ae150_p_d_a0038_vdo_evt",
			.owner = THIS_MODULE,
			.of_match_table = lcm_of_match,
		},
};
module_mipi_dsi_driver(lcm_driver);
MODULE_AUTHOR("xian.zhang <xian.zhang@tinno.com>");
MODULE_DESCRIPTION("csot nt36532c 2800*2000 120Hz DPHY sPanel Driver");
MODULE_LICENSE("GPL");

