#!/bin/bash

if [[ -f /usr/bin/apt-get ]]
then
    apt-get install dialog libncursesw5 libncursesw5-dev
fi
