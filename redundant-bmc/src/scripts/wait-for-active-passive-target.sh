#!/bin/bash

# Wait for the Role property to get set to Active or Passive and
# then wait for the corresponding obmc-bmc-<role>.target to start.
# On the active BMC, then wait up to an additional 15 minutes for
# the file /run/openbmc/bmc_redundancy_determined to be present.

retries=600 # Wait max 20 minutes for role to be set
passive=0
while [ "$retries" -ne 0 ]
do
    role=$(busctl get-property xyz.openbmc_project.State.BMC.Redundancy /xyz/openbmc_project/state/bmc0 xyz.openbmc_project.State.BMC.Redundancy Role)

    if  echo "$role" | grep -q "Active" ; then
        break;
    elif  echo "$role" | grep -q "Passive" ; then
        passive=1
        break;
    fi
    retries="$((retries - 1))"
    sleep 2
done

if [ "$retries" -eq 0 ];
then
    echo "Timed out waiting for BMC role"
    exit 1
fi

target="obmc-bmc-active.target"
if [ "$passive" -eq 1 ];
then
    target="obmc-bmc-passive.target"
fi

echo "BMC role determined, now waiting for $target to start"

unit=$(busctl call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager GetUnit s $target)
# returns: 'o "<unit>"'
unit=${unit//o /} # remove the 'o '
unit=${unit//\"/} # remove the "

retries=1800 # Wait max 60 minutes for target to start
while [ "$retries" -ne 0 ]
do
    state=$(busctl get-property org.freedesktop.systemd1 "$unit" org.freedesktop.systemd1.Unit ActiveState)

    if echo "$state" | grep -q -e '\"active\"' -e '\"failed\"' ;
    then
        echo "Done waiting for $target"
        break
    fi
    retries="$((retries - 1))"
    sleep 2
done

if [ "$retries" -eq 0 ];
then
    echo "Timed out waiting for $target to start"
    exit 1
fi

# If this is the active BMC, wait for redundancy to be determined
if [ "$passive" -eq 0 ];
then
    echo "Active BMC target started, now waiting for redundancy to be determined"

    retries=450 # Wait max 15 minutes for redundancy determined file
    while [ "$retries" -ne 0 ]
    do
        if [ -f /run/openbmc/bmc_redundancy_determined ];
        then
            echo "Redundancy determined file found"
            exit 0
        fi
        retries="$((retries - 1))"
        sleep 2
    done

    echo "Timed out waiting for redundancy to be determined"
    exit 1
fi

exit 0
