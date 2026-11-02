################################################################################
#
# boardlab
#
################################################################################

BOARDLAB_SITE = $(TOPDIR)/../app/boardlab
BOARDLAB_SITE_METHOD = local
BOARDLAB_LICENSE = Proprietary
BOARDLAB_LICENSE_FILES = LICENSE

# The embedded preview uses a direct GStreamer appsink V4L2 pipeline. Qt
# Multimedia/CameraBin is deliberately not part of the package contract.
BOARDLAB_DEPENDENCIES = qt5base qt5declarative qt5quickcontrols2 qt5wayland weston util-linux \
	gstreamer1 gst1-plugins-base gst1-plugins-good

BOARDLAB_CONF_OPTS = \
	-DBOARDLAB_REQUIRE_GSTREAMER_APPSINK=ON \
	-DBOARDLAB_INSTALL_BUNDLED_RKNN_RUNTIME=OFF \
	-DBOARDLAB_RKNN_PROVIDER=$(if $(BR2_PACKAGE_BOARDLAB_NPU),SYSTEM,OFF) \
	-DBOARDLAB_ENABLE_NPU_YOLO=$(if $(BR2_PACKAGE_BOARDLAB_NPU),ON,OFF) \
	-DBOARDLAB_REQUIRE_NPU_YOLO=$(if $(BR2_PACKAGE_BOARDLAB_NPU),ON,OFF)

ifeq ($(BR2_PACKAGE_BOARDLAB_NPU),y)
BOARDLAB_DEPENDENCIES += rknpu2 opencv4
endif

define BOARDLAB_INSTALL_TARGET_CMDS_EXTRA
	# BoardLab owns the Weston session. Remove a stale QLauncher installation
	# when a Buildroot TARGET_DIR is reused after switching product profiles.
	rm -f $(TARGET_DIR)/etc/init.d/S50launcher $(TARGET_DIR)/usr/bin/QLauncher
	if [ -f $(BUILD_DIR)/packages-file-list.txt ]; then \
		$(SED) '\|^QLauncher,\./etc/init.d/S50launcher$$|d' \
			$(BUILD_DIR)/packages-file-list.txt; \
		$(SED) '\|^QLauncher,\./usr/bin/QLauncher$$|d' \
			$(BUILD_DIR)/packages-file-list.txt; \
	fi
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/boardlab
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/lib/boardlab/secrets
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/lib/boardlab/uploads
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/log/boardlab
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/var/cache/boardlab
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/run/boardlab
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/usr/lib/boardlab
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/NetworkManager/conf.d
	$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/boardlab.env \
		$(TARGET_DIR)/etc/boardlab/boardlab.env
	$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/weston-boardlab.ini \
		$(TARGET_DIR)/etc/boardlab/weston-boardlab.ini
	$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/weston-desktop.ini \
		$(TARGET_DIR)/etc/boardlab/weston-desktop.ini
	$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/boardlab-supervisor \
		$(TARGET_DIR)/usr/sbin/boardlab-supervisor
	$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/S49weston-boardlab \
		$(TARGET_DIR)/etc/init.d/S49weston
	if [ -f $(BOARDLAB_PKGDIR)/boardlab-npu-load ]; then \
		$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/boardlab-npu-load \
			$(TARGET_DIR)/usr/lib/boardlab/boardlab-npu-load; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/board-profile.json ]; then \
		$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/board-profile.json \
			$(TARGET_DIR)/etc/boardlab/board-profile.json; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/00-boardlab-main.conf ]; then \
		$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/00-boardlab-main.conf \
			$(TARGET_DIR)/etc/NetworkManager/conf.d/00-boardlab-main.conf; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/99-boardlab-wifi.conf ]; then \
		$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/99-boardlab-wifi.conf \
			$(TARGET_DIR)/etc/NetworkManager/conf.d/99-boardlab-wifi.conf; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/S43wpa_supplicant ]; then \
		$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/S43wpa_supplicant \
			$(TARGET_DIR)/etc/init.d/S43wpa_supplicant; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/S46boardlab-net-prep ]; then \
		$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/S46boardlab-net-prep \
			$(TARGET_DIR)/etc/init.d/S40boardlab-net-prep; \
	fi
	if [ -f $(BOARDLAB_PKGDIR)/99-boardlab.rules ]; then \
		$(INSTALL) -D -m 0644 $(BOARDLAB_PKGDIR)/99-boardlab.rules \
			$(TARGET_DIR)/etc/udev/rules.d/99-boardlab.rules; \
	fi
endef
BOARDLAB_POST_INSTALL_TARGET_HOOKS += BOARDLAB_INSTALL_TARGET_CMDS_EXTRA

ifeq ($(BR2_PACKAGE_BOARDLAB_AUTOSTART),y)
define BOARDLAB_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BOARDLAB_PKGDIR)/S60boardlab \
		$(TARGET_DIR)/etc/init.d/S60boardlab
endef
endif

$(eval $(cmake-package))
