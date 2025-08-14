#!/bin/bash

PACKAGES="dialog libncursesw5 libncursesw5-dev pkgconf"

if [[ -x `which apt-get` ]]
then
    sudo apt-get install $PACKAGES
else
    echo "Can't find apt-get. Probably you need to install '$PACKAGES' packages manually."
fi
