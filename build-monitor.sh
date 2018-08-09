cd /home/pi/app
#gcc test.c -Wall -o test `pkg-config --cflags --libs glib-2.0` -lwiringPi -I/usr/include/postgresql -L/usr/lib/arm-linux/gnueabihf -lpq
gcc monitor.c -Wall -o cwmonitor -lwiringPi -I/usr/include/postgresql -L/usr/lib/arm-linux/gnueabihf -lpq -lm -ldl
cd -
