##Copyright (C) 2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=dreamflower_app
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/dreamflower_app
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Frame buffer device testing tool
  DEPENDS:=+libncurses +libpcap +libpthread
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

TARGET_LDFLAGS :=

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
	CC="$(TARGET_CC)" \
	CFLAGS="$(TARGET_CFLAGS) -Wall" \
	LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/dreamflower_app/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dreamflower_app $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,dreamflower_app))

