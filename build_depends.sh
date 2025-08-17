#!/bin/bash

PACKAGES="make gcc pkgconf dialog libncurses-dev"

if [[ -x `which apt-get` ]]
then
    sudo apt-get -y install $PACKAGES
else
    echo "Can't find apt-get. Probably you need to install '$PACKAGES' packages manually."
fi
