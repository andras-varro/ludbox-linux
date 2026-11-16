# Package makefiles of this external tree would be included here; there are none yet.
#
# make legal-info aborts on the stock linux-headers package with a header tree other than its own 6.x one:
# its hash file carries the licence hashes of Buildroot's own 6.x trees, and its licence list names
# LICENSES/ files whose hashes it does not carry for 7.3.4. This file is included after every package/*/*.mk,
# so the two variables below replace the package's own. A hash file under
# BR2_GLOBAL_PATCH_DIR does not work: it is checked together with the package's own, and
# the first mismatching entry is fatal.
LINUX_HEADERS_HASH_FILES = $(BR2_EXTERNAL_ARCA_PATH)/package-hashes/linux-headers.hash
LINUX_HEADERS_LICENSE_FILES = COPYING

# gcc 13.4's libsanitizer includes <linux/scc.h>, which Linux 7.x headers no longer ship, so
# host-gcc-final fails in all-target-libsanitizer. Nothing on the device uses the sanitizers.
HOST_GCC_COMMON_CONF_OPTS += --disable-libsanitizer
