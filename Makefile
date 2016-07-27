# Copyright 2016 Ben Trask
# MIT licensed (see LICENSE for details)

ROOT_DIR := .
BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps

CFLAGS += -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500
CFLAGS += -g -fno-omit-frame-pointer
CFLAGS += -fstack-protector
CFLAGS += -I$(DEPS_DIR)


WARNINGS := -Werror -Wall -Wextra -Wunused -Wuninitialized -Wvla

# TODO: Unsupported under Clang.
#WARNINGS += -Wlogical-op

# Disabled because it causes a lot of problems on Raspbian (GCC 4.6.3)
# without much perceivable benefit.
#WARNINGS += -Wshadow

# TODO: Useful with GCC but Clang doesn't like it.
#WARNINGS += -Wmaybe-uninitialized

# Causes all string literals to be marked const.
# This would be way too annoying if we don't use const everywhere already.
# The only problem is uv_buf_t, which is const sometimes and not others.
WARNINGS += -Wwrite-strings

# A function's interface is an abstraction and shouldn't strictly reflect
# its implementation. I don't believe in cluttering the code with UNUSED(X).
WARNINGS += -Wno-unused-parameter

# Seems too noisy for static functions in headers.
WARNINGS += -Wno-unused-function

# For OS X.
WARNINGS += -Wno-deprecated

# We define our own Objective-C root class (SLNObject) because we don't use
# Apple's frameworks. Warning only used by Clang. GCC complains about it when
# it stops on an unrelated error, but otherwise it doesn't cause any problems.
WARNINGS += -Wno-objc-root-class

# We use use the isa instance variable when checking that all of the other
# instance variables are zeroed.
WARNINGS += -Wno-deprecated-objc-isa-usage

# Checking that an unsigned variable is less than a constant which happens
# to be zero should be okay.
WARNINGS += -Wno-type-limits

# Usually happens for a ssize_t after already being checked for non-negative,
# or a constant that I don't want to stick a "u" on.
WARNINGS += -Wno-sign-compare

# Checks that format strings are literals amongst other things.
WARNINGS += -Wformat=2



OBJECTS := $(BUILD_DIR)/src/server.o


STATIC_LIBS += $(DEPS_DIR)/libasync/build/libasync.a
CFLAGS += -I$(DEPS_DIR)/libasync/include
CFLAGS += -iquote $(DEPS_DIR)/libasync/deps

# TODO: Switch to LevelDB as soon as we can figure out how to make it work.
STATIC_LIBS += $(DEPS_DIR)/libkvstore/build/libkvstore.a
#STATIC_LIBS += $(DEPS_DIR)/libkvstore/deps/leveldb/libleveldb.a
STATIC_LIBS += $(DEPS_DIR)/libkvstore/deps/liblmdb/liblmdb.a
CFLAGS += -I$(DEPS_DIR)/libkvstore/include


.PHONY: all
all: $(BUILD_DIR)/hash-archive

$(BUILD_DIR)/hash-archive: $(OBJECTS) $(STATIC_LIBS)
	@- mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARNINGS) $(OBJECTS) $(STATIC_LIBS) -o $@

$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c | libasync libkvstore
	@- mkdir -p $(dir $@)
	@- mkdir -p $(dir $(BUILD_DIR)/h/src/$*.d)
	$(CC) -c $(CFLAGS) $(WARNINGS) -MMD -MP -MF $(BUILD_DIR)/h/src/$*.d -o $@ $<

# TODO: Find files in subdirectories without using shell?
-include $(shell find $(BUILD_DIR)/h -name "*.d")

.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	- $(MAKE) distclean -C $(DEPS_DIR)/libasync
	- $(MAKE) distclean -C $(DEPS_DIR)/libkvstore


# TODO: Have libasync bundle these directly.
$(DEPS_DIR)/libasync/build/libasync.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/tls/.libs/libtls.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/ssl/.libs/libssl.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/crypto/.libs/libcrypto.a: | libasync
$(DEPS_DIR)/libasync/deps/uv/.libs/libuv.a: | libasync
.PHONY: libasync
libasync:
	DB=leveldb $(MAKE) -C $(DEPS_DIR)/libasync --no-print-directory

$(DEPS_DIR)/libkvstore/build/libkvstore.a: | libkvstore
$(DEPS_DIR)/libkvstore/deps/leveldb/libleveldb.a: | libkvstore
.PHONY: libkvstore
libkvstore:
	$(MAKE) -C $(DEPS_DIR)/libkvstore --no-print-directory

