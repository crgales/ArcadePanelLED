#!/bin/bash

# Initialize the LED marquee environment
echo "Initializing LED marquee environment..."

python3 -m venv venv
source venv/bin/activate

pip install -r requirements.txt
echo "LED marquee environment initialized."

deactivate

# Integrate script with Batocera system
mkdir -p /userdata/system/scripts
cp Batocera/ledmarquee_game_trigger.sh /userdata/system/scripts/ledmarquee_game_trigger.sh
chmod +x /userdata/system/scripts/ledmarquee_game_trigger.sh