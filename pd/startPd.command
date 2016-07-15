ssh pi@dyew-rpi.local "sudo service fake-ola restart"

ssh -X pi@dyew-rpi.local "sudo killall pd ; /usr/local/bin/pd -nodac -noadc /home/pi/dyew/pd/Main_sensor-processing.pd" && exit 0
