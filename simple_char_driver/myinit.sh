insmod simple_char_driver.ko
mknod /dev/mycdev c 250 0
chmod +666 /dev/mycdev
