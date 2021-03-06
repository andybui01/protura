
TESTS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TESTS_DIR := $(TESTS_DIR:/=)

TEST_DISKS_DIR := $(OBJ_DIR)/test-disks

$(eval $(call dir_rule,$(TEST_RESULTS_DIR)/ktest))
$(eval $(call dir_rule,$(TEST_RESULTS_DIR)/ext2))
$(eval $(call dir_rule,$(TEST_RESULTS_DIR)/pci))
$(eval $(call dir_rule,$(TEST_RESULTS_DIR)/uapi))
$(eval $(call dir_rule,$(TEST_RESULTS_DIR)/symbol-table))
$(eval $(call dir_rule,$(TEST_DISKS_DIR)))

EXTRA_TEST_ARGS :=

ifdef MODULE
EXTRA_TEST_ARGS += ktest.module=$(MODULE)
endif

ifdef SINGLE_TEST
EXTRA_TEST_ARGS += ktest.single_test=$(SINGLE_TEST)
endif

# Wraps the test scripts and provides some extra functionality
ASSERT_WRAPPER=./tests/scripts/assert.sh

# For the ext2 tests, we have scripts the generate test ext2 disks with various
# parameters.
#
# We have separate rule for creating the disk images, so we compile all the
# testcase names here, which we used to then determine the names of all the
# disk images to test with, as well as the test results directory the results
# should go in.
TEST_DISK_SCRIPTS := $(wildcard $(TESTS_DIR)/testcases/test-disks/*.sh)
TEST_DISK_CASES := $(patsubst $(TESTS_DIR)/testcases/test-disks/%.sh,%,$(TEST_DISK_SCRIPTS))

TEST_DISK_IMGS := $(patsubst %,$(TEST_DISKS_DIR)/%.img,$(TEST_DISK_CASES))
TEST_DISK_RESULTS_DIRS := $(patsubst %,$(TEST_RESULTS_DIR)/ext2/%,$(TEST_DISK_CASES))

# Define rules all the required test result directories for the ext2 testcases
$(foreach dir,$(TEST_DISK_RESULTS_DIRS),$(eval $(call dir_rule,$(dir))))
$(foreach disk,$(TEST_DISK_CASES),$(eval $(call dir_rule,$(DISK_MOUNT)/$(disk))))

PHONY += test-disks
test-disks: $(TEST_DISK_IMGS)

$(TEST_DISKS_DIR)/%.img: $(TESTS_DIR)/testcases/test-disks/%.sh | $(TEST_DISKS_DIR) $(DISK_MOUNT)/%
	@echo " TSTDISK $@"
	$(Q)sudo rm -fr $@
	$(Q)sudo $< $@ "$(DISK_MOUNT)/$*"

EXT2_TESTS :=

define check_test_disk_rule
EXT2_TESTS += check-ext2-$(1)

check-ext2-$(1): $$(TEST_DISKS_DIR)/$(1).img $$(IMGS_DIR)/disk.img $$(KERNEL) | $$(TEST_RESULTS_DIR)/ext2/$(1)
	@echo " EXT2TST $(1)"
	$(Q)$(ASSERT_WRAPPER) ./tests/scripts/run_ext2_tests.sh $$(KERNEL) $$(IMGS_DIR)/disk.img $$(TEST_DISKS_DIR)/$(1).img "$$(TEST_RESULTS_DIR)/ext2/$(1)" "$(1)"
endef

$(foreach disk,$(TEST_DISK_CASES),$(eval $(call check_test_disk_rule,$(disk))))

PHONY += check
check: check-uapi check-kernel check-ext2 check-pci check-symbol-table

PHONY += check-kernel
check-kernel: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(TEST_RESULTS_DIR)/ktest
	$(Q)$(ASSERT_WRAPPER) ./tests/scripts/run_ktest_tests.sh $(KERNEL) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(TEST_RESULTS_DIR)/ktest "$(EXTRA_TEST_ARGS)"

PHONY += check-ext2
check-ext2: $(EXT2_TESTS)

PHONY += check-uapi
check-uapi: | $(TEST_RESULTS_DIR)/uapi
	$(Q)$(ASSERT_WRAPPER) ./tests/scripts/verify_uapi.sh "$(CC)" "$(TEST_RESULTS_DIR)/uapi" "./include/uapi" "./arch/$(ARCH)/include/uapi"

PHONY += check-symbol-table
check-symbol-table: $(KERNEL).full $(OBJ_DIR)/vmprotura_symbols2.c | $(TEST_RESULTS_DIR)/symbol-table
	$(Q)$(ASSERT_WRAPPER) ./tests/scripts/verify_symbol_table.sh $(KERNEL).full $(OBJ_DIR)/vmprotura_symbols2.c "$(TEST_RESULTS_DIR)/symbol-table"

PHONY += check-pci
check-pci: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(TEST_RESULTS_DIR)/pci
	$(Q)$(ASSERT_WRAPPER) ./tests/scripts/run_pci_tests.sh $(KERNEL) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(TEST_RESULTS_DIR)/pci

PHONY += debug
debug: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(LOGS_DIR)
	$(Q)./scripts/start_debug_session.sh $(LOGS_DIR) $(KERNEL) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img "-curses" "video=off"

PHONY += debug-ktest
debug-ktest: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(LOGS_DIR)
	$(Q)./scripts/start_debug_session.sh $(LOGS_DIR) $(KERNEL) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img "-curses" "video=off ktest.run=true $(EXTRA_TEST_ARGS)"

PHONY += debug-vga
debug-vga: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(LOGS_DIR)
	$(Q)./scripts/start_debug_session.sh $(LOGS_DIR) $(KERNEL) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img "-vga std"

PHONY += debug-net
debug-net: $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img $(KERNEL) | $(LOGS_DIR)
	$(Q)./scripts/start_net_debug_session.sh $(LOGS_DIR) $(IMGS_DIR)/disk.img $(IMGS_DIR)/disk2.img
