#!/bin/bash

# Initialize the LED marquee environment
echo "Initializing LED marquee environment..."

python3 -m venv venv
source venv/bin/activate

pip install -r requirements.txt
echo "LED marquee environment initialized."

deactivate