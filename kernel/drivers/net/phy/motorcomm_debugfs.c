/* SPDX-License-Identifier: GPL-2.0+ */
#include <linux/netdevice.h>
#include <linux/debugfs.h>

#include <linux/phy.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#define PHY_LINK_STATE "phylink_state"
#define PHYREG_DUMP "dump"
#define DRV_STRENGTH "drive_strength"
#define PHYDRV_VER "phydrv_ver"
#define TEMPLATE "template"
#define LOOPBACK "loopback"
#define CHECKER "checker"
#define GENERATOR "generator"
#define PHYREG "phyreg"
#define MODULE_NAME "yt"
#define CSD "csd"
#define SNR "snr"
#define DELIMITER " \t\n"

#define PHY_ID_YT8510			0x00000109
#define PHY_ID_YT8511			0x0000010a
#define PHY_ID_YT8512			0x00000128
#define PHY_ID_YT8522			0x4f51e928
#define PHY_ID_YT8521			0x0000011a
#define PHY_ID_YT8531S			0x4f51e91a
#define PHY_ID_YT8531			0x4f51e91b
/* YT8543 phy driver disable(default) */
/* #define YTPHY_YT8543_ENABLE */
#ifdef YTPHY_YT8543_ENABLE
#define PHY_ID_YT8543			0x0008011b
#endif
#define PHY_ID_YT8614			0x4f51e899
#define PHY_ID_YT8614Q			0x4f51e8a9
#define PHY_ID_YT8618			0x4f51e889
#define PHY_ID_YT8821			0x4f51ea19
#define PHY_ID_YT8111			0x4f51e8e9
#define PHY_ID_YT8628			0x4f51e8c8
#define PHY_ID_YT8824			0x4f51e8b8

extern char *ytphy_linux_version;

int yt8531_loopback(struct phy_device *phydev, char *type, char *mode,
		    char *speed);
ssize_t yt_loopback_cb(struct file *file, const char __user *user_buf,
		       size_t count, loff_t *ppos);
int yt8531_generator(struct phy_device *phydev, char *mode);
ssize_t yt_generator_cb(struct file *file, const char __user *user_buf,
		        size_t count, loff_t *ppos);
int yt8531_checker(struct phy_device *phydev, char *mode);
ssize_t yt_checker_cb(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos);
int yt8821_template(struct phy_device *phydev, char *speed, char *mode,
		    char *tone);
int yt8531_template(struct phy_device *phydev, char *speed, char *mode,
		    char *tone);
ssize_t yt_template_cb(struct file *file, const char __user *user_buf,
		       size_t count, loff_t *ppos);
int yt8821_snr(struct phy_device *phydev);
int yt8531_snr(struct phy_device *phydev);
ssize_t yt_snr_cb(struct file *file, const char __user *user_buf,
		  size_t count, loff_t *ppos);
int yt8821_csd(struct phy_device *phydev);
int yt8531_csd(struct phy_device *phydev);
ssize_t yt_csd_cb(struct file *file, const char __user *user_buf,
		  size_t count, loff_t *ppos);
int yt8531_drv_strength(struct phy_device *phydev);
ssize_t yt_drv_strength_cb(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos);
int yt8531_phyreg_dump(struct phy_device *phydev, char *reg_space);
int yt8531S_phyreg_dump(struct phy_device *phydev, char *reg_space);
int yt8821_phyreg_dump(struct phy_device *phydev, char *reg_space);
ssize_t yt_phyreg_dump_cb(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos);
ssize_t yt_phyreg_cb(struct file *file, const char __user *user_buf,
		     size_t count, loff_t *ppos);
int yt_phydrv_ver_show(struct seq_file *m, void *v);
int yt_debugfs_init(void);
void yt_debugfs_remove(void);

#define MOTORCOMM_DEBUG_READ_ENTRY(name)					\
	static int yt_##name##_open(struct inode *inode, struct file *file)	\
	{									\
		return single_open(file, yt_##name##_show, inode->i_private);	\
	}									\
										\
	static const struct file_operations yt_##name##_fops = {		\
		.owner		= THIS_MODULE,					\
		.open		= yt_##name##_open,				\
		.read		= seq_read,					\
		.write		= NULL,						\
	}

#define MOTORCOMM_DEBUG_WRITE_ENTRY(name)					\
	static int yt_##name##_open(struct inode *inode, struct file *file)	\
	{									\
		file->private_data = inode->i_private;				\
		return 0;							\
	}									\
										\
	static const struct file_operations yt_##name##_fops = {		\
		.owner		= THIS_MODULE,					\
		.open		= yt_##name##_open,				\
		.read		= NULL,						\
		.write		= yt_##name##_cb,				\
	}

struct ytphy_features_dbg_map {
	u32 phyid;
	int (*phyreg_dump)(struct phy_device *phydev, char *reg_space);
	int (*drive_strength)(struct phy_device *phydev);
	int (*csd)(struct phy_device *phydev);
	int (*snr)(struct phy_device *phydev);
	int (*template)(struct phy_device *phydev, char *speed, char *mode,
			char *tone);
	int (*checker)(struct phy_device *phydev, char *mode);
	int (*generator)(struct phy_device *phydev, char *mode);
	int (*loopback)(struct phy_device *phydev, char *type, char *mode,
			char *speed);
};

extern int ytphy_select_page(struct phy_device *phydev, int page);
extern int ytphy_restore_page(struct phy_device *phydev, int oldpage, int ret);

extern int __ytphy_read_ext(struct phy_device *phydev, u32 regnum);
extern int __ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val);
extern int __ytphy_read_mmd(struct phy_device* phydev, u16 device, u16 reg);
extern int __ytphy_write_mmd(struct phy_device* phydev, u16 device,
							 u16 reg, u16 value);
extern int __phy_write(struct phy_device *phydev, u32 regnum, u16 val);
extern int __phy_read(struct phy_device *phydev, u32 regnum);
extern int ytphy_read_ext(struct phy_device *phydev, u32 regnum);
extern int ytphy_write_ext(struct phy_device *phydev, u32 regnum, u16 val);
__attribute__((unused))
extern int ytphy_read_mmd(struct phy_device* phydev, u16 device, u16 reg);
__attribute__((unused))
extern int ytphy_write_mmd(struct phy_device* phydev, u16 device, u16 reg,
						   u16 value);

static const struct ytphy_features_dbg_map features_dbg[] = {
	{
		.phyid = PHY_ID_YT8821,
		.phyreg_dump = yt8821_phyreg_dump,
		.drive_strength = NULL,
		.csd = yt8821_csd,
		.snr = yt8821_snr,
		.template = yt8821_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	}, {
		.phyid = PHY_ID_YT8531,
		.phyreg_dump = yt8531_phyreg_dump,
		.drive_strength = yt8531_drv_strength,
		.csd = yt8531_csd,
		.snr = yt8531_snr,
		.template = yt8531_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	}, {
		.phyid = PHY_ID_YT8531S,
		.phyreg_dump = yt8531S_phyreg_dump,
		.drive_strength = yt8531_drv_strength,
		.csd = yt8531_csd,
		.snr = yt8531_snr,
		.template = yt8531_template,
		.checker = yt8531_checker,
		.generator = yt8531_generator,
		.loopback = yt8531_loopback,
	},
};

/*
 * utp(1)
 * sds(2)
 * 	internal loopback(1)
 * 	external loopback(2)
 * 	remote loopback(3)
 * 		10M(1)
 * 		100M(2)
 * 		1000M(3)
 * 		2500M(4)
 * eg,
 * utp internal loopback 10M(1 1 1)
 * sds remote loopback 1000M(2 3 3)
 */
int yt8531_loopback(struct phy_device *phydev, char *type, char *mode,
		    char *speed)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(type, "1", 1)) {	/* utp */
		/* utp internal loopback */
		if (!strncasecmp(mode, "1", 1)) {
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			if (phydev->phy_id == PHY_ID_YT8821) {
				ret = __ytphy_read_ext(phydev, 0x27);
				if (ret < 0)
					goto err_restore_page;

				ret &= (~BIT(15));
				ret = __ytphy_write_ext(phydev, 0x27, ret);
				if (ret < 0)
					goto err_restore_page;
			}

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				ret = __phy_write(phydev, 0x0, 0x4100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_ext(phydev, 0x39e,
								0);
					if (ret < 0)
						goto err_restore_page;

					ret = __ytphy_write_ext(phydev, 0x39f,
								0);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x6100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				ret = __phy_write(phydev, 0x0, 0x4140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x0, 0x6140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else if (!strncasecmp(mode, "2", 1)) {
			/* utp external loopback */
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0x27);
			if (ret < 0)
				goto err_restore_page;

			ret &= ~BIT(15);
			ret = __ytphy_write_ext(phydev, 0x27, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(4);
			ret = __ytphy_write_ext(phydev, 0xa, ret);
			if (ret < 0)
				goto err_restore_page;

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __phy_write(phydev, 0x9, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c41);
					if (ret < 0)
						goto err_restore_page;

					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x0, 0x9000);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x0, 0x8100);
					if (ret < 0)
						goto err_restore_page;
				}
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				ret = __phy_write(phydev, 0x0, 0xa100);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __phy_write(phydev, 0x10, 0x2);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x8140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x10, 0x2);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x0, 0xa140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else if (!strncasecmp(mode, "3", 1)) {
			/* utp remote loopback */
			oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa006);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(5);
			ret = __ytphy_write_ext(phydev, 0xa006, ret);
			if (ret < 0)
				goto err_restore_page;

			if (!strncasecmp(speed, "1", 1)) {
				/* 10M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c41);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1061);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "2", 1)) {
				/* 100M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1d01);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1181);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "3", 1)) {
				/* 1000M */
				ret = __phy_write(phydev, 0x9, 0x200);
				if (ret < 0)
					goto err_restore_page;

				if (phydev->phy_id == PHY_ID_YT8821) {
					ret = __ytphy_write_mmd(phydev, 0x7,
								0x20, 0x0);
					if (ret < 0)
						goto err_restore_page;

					ret = __phy_write(phydev, 0x4, 0x1c01);
					if (ret < 0)
						goto err_restore_page;
				} else {
					ret = __phy_write(phydev, 0x4, 0x1001);
					if (ret < 0)
						goto err_restore_page;
				}

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(speed, "4", 1) &&
					phydev->phy_id == PHY_ID_YT8821) {
				/* 2500M */
				ret = __phy_write(phydev, 0x9, 0x0);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x4, 0x1c01);
				if (ret < 0)
					goto err_restore_page;

				ret = __phy_write(phydev, 0x0, 0x9140);
				if (ret < 0)
					goto err_restore_page;
			} else
				pr_err("speed(%s) not support.\n", speed);
		} else {
			pr_err("mode(%s) not support.\n", mode);
			return 0;
		}
	} else if (!strncasecmp(type, "2", 1)) {	/* sds */
		/* sds internal loopback */
		if (!strncasecmp(mode, "1", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __phy_read(phydev, 0x0);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(14);
			ret = __phy_write(phydev, 0x0, ret);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			/* sds external loopback */
			oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;
			/* do nothing */
		} else if (!strncasecmp(mode, "3", 1) &&
			(phydev->phy_id == PHY_ID_YT8531S ||
			phydev->phy_id == PHY_ID_YT8821)) {
			/* sds remote loopback */
			oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
			if (oldpage < 0)
				goto err_restore_page;

			ret = __ytphy_read_ext(phydev, 0xa006);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(6);
			ret = __ytphy_write_ext(phydev, 0xa006, ret);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_read(phydev, 0x0);
			if (ret < 0)
				goto err_restore_page;

			ret |= BIT(15);
			ret = __phy_write(phydev, 0x0, ret);
			if (ret < 0)
				goto err_restore_page;
		} else {
			pr_err("mode(%s) not support.\n", mode);
			return 0;
		}
	} else {
		pr_err("type(%s) not support.\n", type);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/*
 * utp(1)
 * sds(2)
 * 	internal loopback(1)
 * 	external loopback(2)
 * 	remote loopback(3)
 * 		10M(1)
 * 		100M(2)
 * 		1000M(3)
 * 		2500M(4)
 * eg,
 * utp internal loopback 10M(1 1 1)
 * sds remote loopback 1000M(2 3 3)
 */
ssize_t yt_loopback_cb(struct file *file, const char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char speed[8] = {0};
	char type[8] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(type, token);
	if (strncasecmp(type, "1", 1) && strncasecmp(type, "2", 1)) {
		pr_err("type(%s) not support.\n", type);
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1) &&
		strncasecmp(mode, "3", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(speed, token);
	if (strncasecmp(speed, "1", 1) && strncasecmp(speed, "2", 1) &&
		strncasecmp(speed, "3", 1) && strncasecmp(speed, "4", 1)) {
		pr_err("speed(%s) not support.\n", speed);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].loopback(phydev, type, mode,
							 speed);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" loopback: echo ethx utp(1)|sgmii(2) internal(1)|external(2)|remote(3) 10M(1)|100M(2)|1000M(3)|2500M(4) > /sys/kernel/debug/yt/loopback\n");
	pr_err(" eg, utp internal loopback 10M, echo ethx 1 1 1 > /sys/kernel/debug/yt/loopback\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(loopback);

/* mode
 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
 */
int yt8531_generator(struct phy_device *phydev, char *mode)
{
	int ret = 0, oldpage = 0;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	/* utp start */
	if (!strncasecmp(mode, "1", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0xa0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* start generator */
		ret |= BIT(12);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* utp stop */
	if (!strncasecmp(mode, "2", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0xa0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* stop generator */
		ret &= ~BIT(12);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* sds start */
	if (!strncasecmp(mode, "3", 1) &&
		(phydev->phy_id == PHY_ID_YT8531S ||
		phydev->phy_id == PHY_ID_YT8821)) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0x1a0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* start generator */
		ret |= BIT(12);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

	/* sds stop */
	if (!strncasecmp(mode, "4", 1) &&
		(phydev->phy_id == PHY_ID_YT8531S ||
		phydev->phy_id == PHY_ID_YT8821)) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable generator ext 0x1a0 bit15:13 3'b100 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~(BIT(14) | BIT(13));

		/* stop generator */
		ret &= ~BIT(12);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* mode
 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
 */
ssize_t yt_generator_cb(struct file *file, const char __user *user_buf,
		        size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	/* mode
	 * utp start(1)/utp stop(2)/sds start(3)/sds stop(4)/
	 */
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1) &&
		strncasecmp(mode, "3", 1) && strncasecmp(mode, "4", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].generator(phydev, mode);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" generator: \t\techo ethx utp(1)|sgmii(2)> /sys/kernel/debug/yt/generator\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(generator);

/* mode
 * utp(1)/sds(2)
 */
int yt8531_checker(struct phy_device *phydev, char *mode)
{
	char chipname[16] = {0};
	int ret = 0, oldpage;
	u16 val_h;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (phydev->phy_id == PHY_ID_YT8531S)
		sprintf(chipname, "%s", "YT8531S");
	else if (phydev->phy_id == PHY_ID_YT8531)
		sprintf(chipname, "%s", "YT8531");
	else if (phydev->phy_id == PHY_ID_YT8821)
		sprintf(chipname, "%s", "YT8821");

	if (!strncasecmp(mode, "1", 1)) {
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable checker ext 0xa0 bit15:14
		 * 2'b10 enable, 2'b01 disable
		 */
		ret = __ytphy_read_ext(phydev, 0xa0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~BIT(14);
		ret = __ytphy_write_ext(phydev, 0xa0, ret);
		if (ret < 0)
			goto err_restore_page;

		/* ib */
		ret = __ytphy_read_ext(phydev, 0xa3);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa5);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa7);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xa8);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xa9);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(64-1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xaa);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xab);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xac);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ib no sfd: %d\n", chipname, ret);

		/* ob */
		ret = __ytphy_read_ext(phydev, 0xad);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xae);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xaf);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xb0);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xb1);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0xb2);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0xb3);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(64-1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb5);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0xb6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s utp ob no sfd: %d\n", chipname, ret);
	}
	
	if (!strncasecmp(mode, "2", 1) &&
		((phydev->phy_id == PHY_ID_YT8531S) ||
		(phydev->phy_id == PHY_ID_YT8821))) {
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		/* enable checker ext 0x1a0 bit15:14
		 * 2'b10 enable
		 * 2'b01 disable
		 */
		ret = __ytphy_read_ext(phydev, 0x1a0);
		if (ret < 0)
			goto err_restore_page;

		ret |= BIT(15);
		ret &= ~BIT(14);
		ret = __ytphy_write_ext(phydev, 0x1a0, ret);
		if (ret < 0)
			goto err_restore_page;

		/* ib */
		ret = __ytphy_read_ext(phydev, 0x1a3);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a5);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a7);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1a8);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1a9);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(64-1518Byte): %d\n",
		       chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1aa);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ab);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ac);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ib no sfd: %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1ad);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1ae);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(64-1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1af);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1b0);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(>1518Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1b1);
		if (ret < 0)
			goto err_restore_page;
		val_h = ret;

		ret = __ytphy_read_ext(phydev, 0x1b2);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob valid(<64Byte): %d\n",
		       chipname, (val_h << 16) | ret);

		ret = __ytphy_read_ext(phydev, 0x1b3);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(64-1518Byte): %d\n",
		       chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b4);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(>1518Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b5);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob crc err(<64Byte): %d\n", chipname, ret);

		ret = __ytphy_read_ext(phydev, 0x1b6);
		if (ret < 0)
			goto err_restore_page;
		pr_err("%s serdes ob no sfd: %d\n", chipname, ret);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* mode
 * utp(1)/sds(2)
 */
ssize_t yt_checker_cb(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char mode[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(mode, token);
	if (strncasecmp(mode, "1", 1) && strncasecmp(mode, "2", 1)) {
		pr_err("mode(%s) not support.\n", mode);
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].checker(phydev, mode);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" checker: \t\techo ethx utp(1)|sgmii(2)> /sys/kernel/debug/yt/checker\n");
	pr_err(" eg, echo ethx 1 > /sys/kernel/debug/yt/checker\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(checker);

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
int yt8821_template(struct phy_device *phydev, char *speed, char *mode,
		    char *tone)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	if (!strncasecmp(speed, "4", 1)) {
		/* 2500M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2010);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_mmd(phydev, 0x1, 0x0, 0x2058);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x2000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x4000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0x6000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "4", 1)) {
			if (!strncasecmp(tone, "1", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x8400);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "2", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x8800);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "3", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9000);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "4", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9400);
				if (ret < 0)
					goto err_restore_page;
			} else if (!strncasecmp(tone, "5", 1)) {
				ret = __ytphy_write_mmd(phydev, 0x1, 0x84,
							0x9800);
				if (ret < 0)
					goto err_restore_page;
			}
		} else if(!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xa000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "6", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xc000);
			if (ret < 0)
				goto err_restore_page;
		} else if(!strncasecmp(mode, "7", 1)) {
			ret = __ytphy_write_mmd(phydev, 0x1, 0x84, 0xe000);
			if (ret < 0)
				goto err_restore_page;
		}
	} else if (!strncasecmp(speed, "3", 1)) {
		/* 1000M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0x8140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __phy_write(phydev, 0x9, 0x2200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __phy_write(phydev, 0x9, 0x5a00);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __phy_write(phydev, 0x9, 0x7200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __phy_write(phydev, 0x9, 0x8200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else if (!strncasecmp(speed, "2", 1)) {
		/* 100M */
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa100);
		if (ret < 0)
			goto err_restore_page;
	} else if (!strncasecmp(speed, "1", 1)) {
		/* 10M */
		ret = __phy_write(phydev, 0x0, 0x8100);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x42a, 0x1);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x414, 0xff01);
		if (ret < 0)
			goto err_restore_page;

		ret = __ytphy_write_ext(phydev, 0x4f2, 0x7777);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x209);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20A);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20B);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20C);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20D);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else
		pr_err("speed(%s) not support.\n", speed);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
int yt8531_template(struct phy_device *phydev, char *speed, char *mode,
		    char *tone)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	if (!strncasecmp(speed, "3", 1)) {
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0x8140);
		if (ret < 0)
			goto err_restore_page;

		if (!strncasecmp(mode, "1", 1)) {
			ret = __phy_write(phydev, 0x9, 0x2200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __phy_write(phydev, 0x9, 0x5a00);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __phy_write(phydev, 0x9, 0x7200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __phy_write(phydev, 0x9, 0x8200);
			if (ret < 0)
				goto err_restore_page;

			ret = __phy_write(phydev, 0x0, 0x8140);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else if (!strncasecmp(speed, "2", 1)) {
		ret = __ytphy_write_ext(phydev, 0x27, 0x2026);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x10, 0x2);
		if (ret < 0)
			goto err_restore_page;

		ret = __phy_write(phydev, 0x0, 0xa100);
		if (ret < 0)
			goto err_restore_page;
	} else if (!strncasecmp(speed, "1", 1)) {
		ret = __phy_write(phydev, 0x0, 0x8100);
		if (ret < 0)
			goto err_restore_page;

		if (phydev->phy_id == PHY_ID_YT8531S) {
			ret = __ytphy_write_ext(phydev, 0xa071, 0x9007);
			if (ret < 0)
				goto err_restore_page;
		}

		if (!strncasecmp(mode, "1", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x209);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "2", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20A);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "3", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20B);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "4", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20C);
			if (ret < 0)
				goto err_restore_page;
		} else if (!strncasecmp(mode, "5", 1)) {
			ret = __ytphy_write_ext(phydev, 0xa, 0x20D);
			if (ret < 0)
				goto err_restore_page;
		} else
			pr_err("speed(%s) mode(%s) not support.\n", speed,
			       mode);
	} else
		pr_err("speed(%s) not support.\n", speed);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/* 
 * 10M(1):
 * test_mode
 * 1: packet with all ones, 10MHz sine wave, for harmonic test
 * 2: pseudo random, for TP_idle/Jitter/Different voltage test
 * 3: normal link pulse only
 * 4: 5MHz sine wave
 * 5: Normal mode
 * 
 * 100M(2):
 * test_mode - NOT CARE
 * 
 * 1000BT(3):
 * test_mode
 * 1: Test Mode 1, Transmit waveform test
 * 2: Test Mode 2, Transmit Jitter test (master mode)
 * 3: Test Mode 3, Transmit Jitter test (slave mode)
 * 4: Test Mode 4, Transmit distortion test
 * 
 * 2500BT(4):
 * 1(test mode1)
 * 2(test mode2)
 * 3(test mode3)
 * 4(test mode4)
 * 	1(Dual tone 1)
 * 	2(Dual tone 2)
 * 	3(Dual tone 3)
 * 	4(Dual tone 4)
 * 	5(Dual tone 5)
 * 5(test mode5)
 * 6(test mode6)
 * 7(test mode7)
 */
ssize_t yt_template_cb(struct file *file, const char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char speed[8] = {0};
	char mode[8] = {0};
	char tone[8] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(speed, token);
	if (strncasecmp(speed, "1", 1) && strncasecmp(speed, "2", 1) &&
		strncasecmp(speed, "3", 1) && strncasecmp(speed, "4", 1)) {
		pr_err("speed(%s) not support.\n", speed);
		goto error;
	}

	if (strncasecmp(speed, "2", 1)) {	/* 100M not care test mode */
		token = strsep(&pos, DELIMITER);
		if (!token) {
			pr_err("register space is not specified.\n");
			goto error;
		}

		strcpy(mode, token);

		if (!strncasecmp(speed, "1", 1))	/* 10M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1) &&
				strncasecmp(mode, "5", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}

		if (!strncasecmp(speed, "3", 1))	/* 1000M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}

		if (!strncasecmp(speed, "4", 1))	/* 2500M */
			if (strncasecmp(mode, "1", 1) &&
				strncasecmp(mode, "2", 1) &&
				strncasecmp(mode, "3", 1) &&
				strncasecmp(mode, "4", 1) &&
				strncasecmp(mode, "5", 1) &&
				strncasecmp(mode, "6", 1) &&
				strncasecmp(mode, "7", 1)) {
				pr_err("speed(%s) mode(%s) not support.\n",
				       speed, mode);
				goto error;
			}
	}

	if (!strncasecmp(speed, "4", 1) && !strncasecmp(mode, "4", 1)) {
		/* 2500M test mode4 */
		token = strsep(&pos, DELIMITER);
		if (!token) {
			pr_err("register space is not specified.\n");
			goto error;
		}

		strcpy(tone, token);
		if (strncasecmp(tone, "1", 1) &&
			strncasecmp(tone, "2", 1) &&
			strncasecmp(tone, "3", 1) &&
			strncasecmp(tone, "4", 1) &&
			strncasecmp(tone, "5", 1)) {
			pr_err("speed(%s) mode(%s) tone(%s) not support.\n",
			       speed, mode, tone);
			goto error;
		}
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].template(phydev, speed, mode,
							 tone);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n"
	       "template(  10M): test mode1(1) - packet with all ones, 10MHz sine wave, for harmonic test\n"
	       "template(  10M): test mode1(2) - pseudo random, for TP_idle/Jitter/Different voltage test\n"
	       "template(  10M): test mode1(3) - normal link pulse only\n"
	       "template(  10M): test mode1(4) - 5MHz sine wave\n"
	       "template(  10M): test mode1(5) - Normal mode\n"
	       "template(  10M): echo ethx 10M(1) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5) > /sys/kernel/debug/yt/template\n");
	pr_err("template( 100M): echo ethx 100M(2) > /sys/kernel/debug/yt/template\n");
	pr_err("template(1000M): test mode1(1) - Transmit waveform test\n"
	       "template(1000M): test mode1(2) - Transmit Jitter test (master mode)\n"
	       "template(1000M): test mode1(3) - Transmit Jitter test (slave mode)\n"
	       "template(1000M): test mode1(4) - Transmit distortion test\n"
	       "template(1000M): echo ethx 1000M(3) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4) > /sys/kernel/debug/yt/template\n");
	pr_err("template(2500M): echo ethx 2500M(4) test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5)|test mode6(6)|test mode7(7)\n"
	       "\t[dual tone1(1)|dual tone2(2)|dual tone3(3)|dual tone4(4)|dual tone5(5)] > /sys/kernel/debug/yt/template\n");
	pr_err("echo ethx 10M(1)|100M(2)|1000M(3)|2500M(4) [test mode1(1)|test mode2(2)|test mode3(3)|test mode4(4)|test mode5(5)|test mode6(6)|test mode7(7)]\n"
	       "\t[dual tone1(1)|dual tone2(2)|dual tone3(3)|dual tone4(4)|dual tone5(5)] > /sys/kernel/debug/yt/template\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(template);

int yt8821_snr(struct phy_device *phydev)
{
	u16 mse0, mse1, mse2, mse3;
	int ret = 0, oldpage;
	
#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* read utp ext 0x51F, 0x520, 0x521, 0x522 bit15:0
	 * and get 4 pair mse of mdi
	 */
	ret = __ytphy_read_ext(phydev, 0x51F);
	if (ret < 0)
		goto err_restore_page;
	mse0 = ret;

	ret = __ytphy_read_ext(phydev, 0x520);
	if (ret < 0)
		goto err_restore_page;
	mse1 = ret;

	ret = __ytphy_read_ext(phydev, 0x521);
	if (ret < 0)
		goto err_restore_page;
	mse2 = ret;

	ret = __ytphy_read_ext(phydev, 0x522);
	if (ret < 0)
		goto err_restore_page;
	mse3 = ret;

	pr_err("mse0: %d, mse1: %d, mse2: %d, mse3: %d\n",
	       mse0, mse1, mse2, mse3);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

int yt8531_snr(struct phy_device *phydev)
{
	u16 mse0, mse1, mse2, mse3;
	int ret = 0, oldpage;
	
#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x59 bit15 */
	ret = __ytphy_read_ext(phydev, 0x59);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x59, ret | BIT(15));
	if (ret < 0)
		goto err_restore_page;

	/* clear utp ext 0xf8 bit10 */
	ret = __ytphy_read_ext(phydev, 0xf8);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0xf8, ret & (~BIT(10)));
	if (ret < 0)
		goto err_restore_page;

	/* wait 500ms */
	mdelay(500);

	/* read utp ext 0x5a, 0x5b, 0x5c, 0x5d bit14:0 */
	ret = __ytphy_read_ext(phydev, 0x5a);
	if (ret < 0)
		goto err_restore_page;
	mse0 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5b);
	if (ret < 0)
		goto err_restore_page;
	mse1 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5c);
	if (ret < 0)
		goto err_restore_page;
	mse2 = ret & (~BIT(15));

	ret = __ytphy_read_ext(phydev, 0x5d);
	if (ret < 0)
		goto err_restore_page;
	mse3 = ret & (~BIT(15));

	pr_err("mse0: %d, mse1: %d, mse2: %d, mse3: %d\n",
	       mse0, mse1, mse2, mse3);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

ssize_t yt_snr_cb(struct file *file, const char __user *user_buf,
		  size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].snr(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" snr: \t\techo ethx > /sys/kernel/debug/yt/snr\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(snr);

int yt8821_csd(struct phy_device *phydev)
{
	u8 ch3_state, ch2_state, ch1_state, ch0_state;
	int ret = 0, oldpage;
	u16 distance;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x80 val 0x468b */
	ret = __ytphy_write_ext(phydev, 0x80, 0x468b);
	if (ret < 0)
		goto err_restore_page;

	/* till utp ext 0x84 bit15 1'b0, csd finished. */
	do {
		ret = __ytphy_read_ext(phydev, 0x84);
		if (ret < 0)
			goto err_restore_page;

		usleep_range(10000, 20000);
	} while (ret & BIT(15));

	/* ext reg 0x85 open & short detect
	 * bit15:14 self_st_33_1
	 * bit13:12 self_st_33_2
	 * bit11:10 self_st_22_1
	 * bit9:8 self_st_22_2
	 * bit7:6 self_st_11_1
	 * bit5:4 self_st_11_2
	 * bit3:2 self_st_00_1
	 * bit1:0 self_st_00_2
	 * 2'b00 normal, 2'b01 error happened during last csd test,
	 * 2'b10 short, 2'b11 open
	 */
	ret = __ytphy_read_ext(phydev, 0x85);
	if (ret < 0)
		goto err_restore_page;

	ch3_state = (ret & 0xc000) >> 14;	/* self_st_33_1 */
	ch2_state = (ret & 0x0c00) >> 10;	/* self_st_22_1 */
	ch1_state = (ret & 0x00c0) >> 6;	/* self_st_11_1 */
	ch0_state = (ret & 0x000c) >> 2;	/* self_st_00_1 */

	if (ch3_state == 0) {
		pr_err("channel 3 is normal\n");
	} else if (ch3_state == 1) {
		pr_err("channel 3 error happened during last csd test.\n");
	} else {
		/* ext reg 0x90, 
		 * The intra pair damage location of channel 3. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x90);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 3: %d(cm).\n",
		       ch3_state == 2 ? "short" :
		       ch3_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch2_state == 0) {
		pr_err("channel 2 is normal\n");
	} else if (ch2_state == 1) {
		pr_err("channel 2 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8e, 
		 * The intra pair damage location of channel 2. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8e);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 2: %d(cm).\n",
		       ch2_state == 2 ? "short" :
		       ch2_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch1_state == 0) {
		pr_err("channel 1 is normal\n");
	} else if (ch1_state == 1) {
		pr_err("channel 1 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8c, 
		 * The intra pair damage location of channel 1. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8c);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 1: %d(cm).\n",
		       ch1_state == 2 ? "short" :
		       ch1_state == 3 ? "open" : "other",
		       distance);
	}

	if (ch0_state == 0) {
		pr_err("channel 0 is normal\n");
	} else if (ch0_state == 1) {
		pr_err("channel 0 error happened during last csd test.\n");
	} else {
		/* ext reg 0x8a, 
		 * The intra pair damage location of channel 0. In unit cm.
		 */
		distance = __ytphy_read_ext(phydev, 0x8a);
		if (distance < 0)
			goto err_restore_page;
		pr_err("The intra pair damage(%s) location of channel 0: %d(cm).\n",
		       ch0_state == 2 ? "short" :
		       ch0_state == 3 ? "open" : "other",
		       distance);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

int yt8531_csd(struct phy_device *phydev)
{
	u8 ch3_state, ch2_state, ch1_state, ch0_state;
	int ret = 0, oldpage;
	u16 distance;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set utp ext 0x98 val 0xb0a6 */
	ret = __ytphy_write_ext(phydev, 0x98, 0xb0a6);
	if (ret < 0)
		goto err_restore_page;

	/* clear utp ext 0x27 bit15 */
	ret = __ytphy_read_ext(phydev, 0x27);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x27, ret & (~BIT(15)));
	if (ret < 0)
		goto err_restore_page;

	/* set utp mii 0x0 0x8000 */
	ret = __phy_write(phydev, 0x0, 0x8000);
	if (ret < 0)
		goto err_restore_page;

	/* set utp ext 0x80 bit0 */
	ret = __ytphy_read_ext(phydev, 0x80);
	if (ret < 0)
		goto err_restore_page;

	ret = __ytphy_write_ext(phydev, 0x80, ret | BIT(0));
	if (ret < 0)
		goto err_restore_page;

	/* till utp ext 0x84 bit15 1'b0, csd finished.
	 * read utp ext 0x84 bit7:0, check normal/open/short of pair
	 */
	do {
		ret = __ytphy_read_ext(phydev, 0x84);
		if (ret < 0)
			goto err_restore_page;

		usleep_range(10000, 20000);
	} while (ret & BIT(15));

	ret = __ytphy_read_ext(phydev, 0x84);
	if (ret < 0)
		goto err_restore_page;

	/* bit7:6 Intra pair status of channel 3
	 * bit5:4 Intra pair status of channel 2
	 * bit3:2 Intra pair status of channel 1
	 * bit1:0 Intra pair status of channel 0
	 * 2'b00 normal, 2'b01 error happened during last csd test,
	 * 2'b10 short, 2'b11 open
	 * read ext 0x87 ~ 0x8a to get pair0 ~ pair3 the distance of damage(cm)
	 */
	ch3_state = (ret & 0xc0) >> 6;
	ch2_state = (ret & 0x30) >> 4;
	ch1_state = (ret & 0x0c) >> 2;
	ch0_state = (ret & 0x03);

	if (ch3_state == 0) {
		pr_err("channel 3 is normal\n");
	} else if (ch3_state == 1) {
		pr_err("channel 3 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x8a);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 3 damage(%s) at %d(cm)\n",
			ch3_state == 2 ? "short" :
			ch3_state == 3 ? "open" : "other",
			distance);
	}

	if (ch2_state == 0) {
		pr_err("channel 2 is normal\n");
	} else if (ch2_state == 1) {
		pr_err("channel 2 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x89);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 2 damage(%s) at %d(cm)\n",
			ch2_state == 2 ? "short" :
			ch2_state == 3 ? "open" : "other",
			distance);
	}

	if (ch1_state == 0) {
		pr_err("channel 1 is normal\n");
	} else if (ch1_state == 1) {
		pr_err("channel 1 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x88);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 1 damage(%s) at %d(cm)\n",
			ch1_state == 2 ? "short" :
			ch1_state == 3 ? "open" : "other",
			distance);
	}

	if (ch0_state == 0) {
		pr_err("channel 0 is normal\n");
	} else if (ch0_state == 1) {
		pr_err("channel 0 error happened during last csd test.\n");
	} else {
		distance = __ytphy_read_ext(phydev, 0x87);
		if (distance < 0)
			goto err_restore_page;

		pr_err("channel 0 damage(%s) at %d(cm)\n",
			ch0_state == 2 ? "short" :
			ch0_state == 3 ? "open" : "other",
			distance);
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

ssize_t yt_csd_cb(struct file *file, const char __user *user_buf,
		  size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].csd(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" csd: \t\techo ethx > /sys/kernel/debug/yt/csd\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(csd);

int yt8531_drv_strength(struct phy_device *phydev)
{
	int ret = 0, oldpage;

#define UTP_REG_SPACE	0x0

	oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __ytphy_read_ext(phydev, 0xa010);
	if (ret < 0)
		goto err_restore_page;
	/*
	 * ext reg 0xa010
	 * bit15:13 rgmii rxc
	 * bit12 bit5:4 rgmii rxd rx_ctl
	 * 111 max, 000 min
	 */
	ret = __ytphy_write_ext(phydev, 0xa010, ret | 0xf030);

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

ssize_t yt_drv_strength_cb(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].drive_strength(phydev);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" rgmii drive strength: \t\techo ethx > /sys/kernel/debug/yt/dump\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(drv_strength);

int yt8531_phyreg_dump(struct phy_device *phydev, char *reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

int yt8531S_phyreg_dump(struct phy_device *phydev, char *reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else if (!strncasecmp(reg_space, "sgmii", 5)) {
		pr_err("sgmii reg space: \n");
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x9; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __phy_read(phydev, 0xf);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0xf, ret);

		ret = __phy_read(phydev, 0x11);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0x11, ret);

		for (idx = 0x14; idx < 0x16; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

int yt8821_phyreg_dump(struct phy_device *phydev, char *reg_space)
{
	int ret = 0, oldpage;
	u8 idx;

#define UTP_REG_SPACE	0x0
#define SGMII_REG_SPACE	0x2

	if (!strncasecmp(reg_space, "utp", 3)) {
		pr_err("utp reg space: \n");
		oldpage = ytphy_select_page(phydev, UTP_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x20; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __ytphy_read_ext(phydev, 0xa001);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa001: \t0x%04x\n", ret);

		ret = __ytphy_read_ext(phydev, 0xa003);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("ext reg 0xa003: \t0x%04x\n", ret);
	} else if (!strncasecmp(reg_space, "sgmii", 5)) {
		pr_err("sgmii reg space: \n");
		oldpage = ytphy_select_page(phydev, SGMII_REG_SPACE);
		if (oldpage < 0)
			goto err_restore_page;

		for (idx = 0; idx < 0x9; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}

		ret = __phy_read(phydev, 0xf);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0xf, ret);

		ret = __phy_read(phydev, 0x11);
		if (ret < 0)
			goto err_restore_page;
		else
			pr_err("mii reg 0x%x: \t0x%04x\n", 0x11, ret);

		for (idx = 0x14; idx < 0x18; idx++) {
			ret = __phy_read(phydev, idx);
			if (ret < 0)
				goto err_restore_page;
			else
				pr_err("mii reg 0x%x: \t0x%04x\n", idx, ret);
		}
	} else {
		pr_err("reg space(%s) not support\n", reg_space);
		return 0;
	}

err_restore_page:
	return ytphy_restore_page(phydev, oldpage, ret);
}

/*
 * ethx udp
 * ethx sds
 */
ssize_t yt_phyreg_dump_cb(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	int tb_size = ARRAY_SIZE(features_dbg);
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char reg_space[16] = {0};
	char kbuf[128] = {0};
	char eth[8] = {0};
	char *token;
	int ret = 0;
	char *pos;
	u8 idx;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) {
		pr_err("register space is not specified.\n");
		goto error;
	}

	strcpy(reg_space, token);
	if (strncasecmp(reg_space, "utp", 3) &&
			strncasecmp(reg_space, "sgmii", 5) &&
			strncasecmp(reg_space, "qsgmii", 6))
		goto error;

	for (idx = 0; idx < tb_size; idx++) {
		if (phydev->phy_id == features_dbg[idx].phyid) {
			ret = features_dbg[idx].phyreg_dump(phydev, reg_space);
			break;
		}
	}

	if (dev)
		dev_put(dev);

	if (ret < 0)
		return ret;
	else
		return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" reg dump: \t\techo ethx reg_space(utp|sgmii|qsgmii) > /sys/kernel/debug/yt/dump\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(phyreg_dump);

/* ethx mii read reg(0x)
 * ethx mii write reg(0x) val(0x)
 * ethx ext read reg(0x)
 * ethx ext write reg(0x) val(0x)
 * ethx mmd read  mmd(0x) reg(0x)
 * ethx mmd write mmd(0x) reg(0x) val(0x)
 */
ssize_t yt_phyreg_cb(struct file *file, const char __user *user_buf,
		     size_t count, loff_t *ppos)
{
	struct net_device *dev = NULL;
	struct phy_device *phydev;
	char kbuf[128] = {0};
	char type[8] = {0};
	char eth[8] = {0};
	char cmd[8] = {0};
	u16 regAddr = 0;
	u16 regData = 0;
	u16 device = 0;
	char *token;
	char *pos;
	int ret;

	if (count == 1)
		goto error;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	pos = kbuf;
	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(eth, token);
	dev = dev_get_by_name(&init_net, eth);
	if (!dev) {
		pr_err("Failed to find net_device for %s\n", eth);
		goto error;
	}

	phydev = dev->phydev;
	if (!phydev) {
		pr_err("Failed to find phy_device\n");
		goto error;
	}

	token = strsep(&pos, DELIMITER);
	if (!token) 
		goto error;

	strcpy(type, token);
	if (strncasecmp(type, "mii", 3) && strncasecmp(type, "ext", 3) &&
			strncasecmp(type, "mmd", 3))
		goto error;

	token = strsep(&pos, DELIMITER);
	if (!token)
		goto error;

	strcpy(cmd, token);
	if (strncasecmp(type, "mmd", 3) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &device);
		if (ret) {
			pr_err("Failed to convert device to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}
	}

	if (strncasecmp(cmd, "write", 5) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regAddr);
		if (ret) {
			pr_err("Failed to convert regAddr to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regData);
		if (ret) {
			pr_err("Failed to convert regData to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		if (strncasecmp(type, "mmd", 3) == 0) {
			pr_err("write mmd = 0x%x reg = 0x%x val = 0x%x\n",
				device, regAddr, regData);
			ytphy_write_mmd(phydev, device, regAddr, regData);
		} else {
			if (strncasecmp(type, "mii", 3) == 0) {
				phy_write(phydev, regAddr, regData);
				pr_err("write mii reg = 0x%x val = 0x%x\n",
				       regAddr, regData);
			} else if (strncasecmp(type, "ext", 3) == 0) {
				ytphy_write_ext(phydev, regAddr, regData);
				pr_err("write ext reg = 0x%x val = 0x%x\n",
				       regAddr, regData);
			}
		}
	} else if (strncasecmp(cmd, "read", 4) == 0) {
		token = strsep(&pos, DELIMITER);
		if (!token)
			goto error;

		ret = kstrtou16(token, 16, &regAddr);
		if (ret) {
			pr_err("Failed to convert regAddr to unsigned short\n");
			if (dev)
				dev_put(dev);
			return ret;
		}

		if (strncasecmp(type, "mmd", 3) == 0) {
			regData = ytphy_read_mmd(phydev, device, regAddr);
			pr_err("read mmd = 0x%x, reg = 0x%x, val = 0x%x\n",
			       device, regAddr, regData);
		} else {
			if (strncasecmp(type, "mii", 3) == 0)
				regData = phy_read(phydev, regAddr);
			else if (strncasecmp(type, "ext", 3) == 0)
				regData = ytphy_read_ext(phydev, regAddr);
			pr_err("read %s reg = 0x%x, val = 0x%x\n", type,
			       regAddr, regData);
		}
	} else
		goto error;

	if (dev)
		dev_put(dev);

	return count;
error:
	if (dev)
		dev_put(dev);
	pr_err("usage:\n");
	pr_err(" read mii reg: \t\techo ethx mii read reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write mii reg val: \techo ethx mii write reg val > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" read ext reg: \t\techo ethx ext read reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write ext reg val: \techo ethx ext write reg val > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" read mmd reg: \t\techo ethx mmd read device reg > /sys/kernel/debug/yt/phyreg\n");
	pr_err(" write mmd reg val: \techo ethx mmd write device reg val > /sys/kernel/debug/yt/phyreg\n");
	return -EFAULT;
}

MOTORCOMM_DEBUG_WRITE_ENTRY(phyreg);

int yt_phydrv_ver_show(struct seq_file *m, void *v) {
	seq_printf(m, "Driver Version: %s\n", ytphy_linux_version);

	return 0;
}

MOTORCOMM_DEBUG_READ_ENTRY(phydrv_ver);

static struct dentry *root_dir;
int yt_debugfs_init(void)
{
	/* Create debugfs directory */
	root_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!root_dir) {
		pr_err(MODULE_NAME ": Failed to create debugfs directory\n");
		return -ENOMEM;
	}

	/* Create debugfs file read only */
	if (!debugfs_create_file(PHYDRV_VER, 0444, root_dir, NULL,
				 &yt_phydrv_ver_fops)) {
		pr_err(PHYDRV_VER ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG_DUMP, 0222, root_dir, NULL,
				 &yt_phyreg_dump_fops)) {
		pr_err(PHYREG_DUMP ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(DRV_STRENGTH, 0222, root_dir, NULL,
				 &yt_drv_strength_fops)) {
		pr_err(DRV_STRENGTH ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(PHYREG, 0222, root_dir, NULL,
				 &yt_phyreg_fops)) {
		pr_err(PHYREG ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(TEMPLATE, 0222, root_dir, NULL,
				 &yt_template_fops)) {
		pr_err(TEMPLATE ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CSD, 0222, root_dir, NULL,
				 &yt_csd_fops)) {
		pr_err(CSD ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(SNR, 0222, root_dir, NULL,
				 &yt_snr_fops)) {
		pr_err(SNR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(CHECKER, 0222, root_dir, NULL,
				 &yt_checker_fops)) {
		pr_err(CHECKER ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(GENERATOR, 0222, root_dir, NULL,
				 &yt_generator_fops)) {
		pr_err(GENERATOR ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	/* Create debugfs file write only */
	if (!debugfs_create_file(LOOPBACK, 0222, root_dir, NULL,
				 &yt_loopback_fops)) {
		pr_err(LOOPBACK ": Failed to create debugfs file\n");
		debugfs_remove_recursive(root_dir);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(yt_debugfs_init);

void yt_debugfs_remove(void)
{
	debugfs_remove_recursive(root_dir);
	pr_info(MODULE_NAME ": Module unloaded\n");
}
EXPORT_SYMBOL_GPL(yt_debugfs_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Motorcomm PHY Driver Debugfs");
