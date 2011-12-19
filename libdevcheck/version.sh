#!/bin/sh

# check for git short hash
revision=`git describe --always 2> /dev/null`

tag=`git tag --contains $revision`

if [[ $tag != '' ]]
then
    version="$tag-g$revision"
else
    version=g$revision
fi

NEW_VERSION="#define WHDD_VERSION \"$version\""
OLD_VERSION=`cat version.h 2> /dev/null`

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_VERSION" != "$OLD_VERSION"; then
    echo "$NEW_VERSION"
fi
