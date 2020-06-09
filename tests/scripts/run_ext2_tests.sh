#!/bin/bash
#
# Runs the kernel and execute a ext2 test, and then parses the output
#
# Argument 1: Kernel image
# Argument 2: First disk image
# Argument 3: Second disk image
# Argument 4: test results directory

KERNEL=$1
DISK_ONE=$2
DISK_TWO=$3
TEST_PREFIX=$4

QEMU_PID=
RET=
DISK_CPY=./obj/disk_cpy.img

. ./tests/scripts/colors.sh

function run_ext2_test {
    echo "QEMU: Test: $1"
    timeout 120 qemu-system-i386 \
        -serial file:$3 \
        -d cpu_reset \
        -drive format=raw,file=$DISK_ONE,cache=none,media=disk,index=0,if=ide \
        -drive format=raw,file=$DISK_CPY,media=disk,index=1,if=ide \
        -display none \
        -no-reboot \
        -kernel $KERNEL \
        -append "init=$1 reboot_on_panic=1" \
        2> /dev/null &

    QEMU_PID=$!
}

function test_verify {
    tail --pid $QEMU_PID -n+1 -f $TEST_LOG

    wait $QEMU_PID

    RET=$?
}

TOTAL_RESULT=0

TESTS=$(find ./userspace/root/tests/ext2/ -name "test_*.sh" | xargs basename -a)

for test in $TESTS; do
    TEST_LOG=${TEST_PREFIX}/$(basename -s .sh $test).qemu.log
    TEST_E2FSCK_LOG=${TEST_PREFIX}/$(basename -s .sh $test).e2fsck.log

    echo "LOG: $TEST_LOG, e2fsck: $TEST_E2FSCK_LOG"

    rm -fr $DISK_CPY
    cp $DISK_TWO $DISK_CPY

    rm -fr $TEST_LOG
    touch $TEST_LOG

    rm -fr $TEST_E2FSCK_LOG
    touch $TEST_E2FSCK_LOG

    run_ext2_test "/tests/ext2/$test" "$DISK_CPY" "$TEST_LOG"
    test_verify

    if [ "$RET" -ne "0" ]; then
        echo "QEMU TIMEOUT" >> "$TEST_LOG"

        printf "$RED QEMU TIMEOUT, DISK LIKELY NOT SYNCED, FAILURE!!$RESET ..."

        TOTAL_RESULT=$(($TOTAL_RESULT + 1))
    fi

    sudo e2fsck -nfv $DISK_CPY | tee $TEST_E2FSCK_LOG

    if [ "${PIPESTATUS[0]}" -ne "0" ]; then
        echo "EXT2 FAILURE" >> "$TEST_E2FSCK_LOG"

        echo "$RED EXT2 FILE SYSTEM ERRORS!$RESET"
        echo "e2fsck results:"
        cat $TEST_E2FSCK_LOG

        TOTAL_RESULT=$(($TOTAL_RESULT + 1))
    else
        echo "EXT2 PASS" >> "$TEST_E2FSCK_LOG"
        echo "$GREEN PASS!$RESET"
    fi
done

rm $DISK_CPY

if [ "$TOTAL_RESULT" == "0" ]; then
    echo "${GREEN}ALL TESTS PASSED!$RESET"
else
    echo "${RED}TESTS FAILURE!$RESET"
fi

exit $TOTAL_RESULT
