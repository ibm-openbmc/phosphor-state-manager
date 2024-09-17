# Redundant BMC Management

The phosphor-rbmc-state-manager application manages redundant BMC functionality.

It hosts an xyz.openbmc_project.State.BMC.Redundancy interface on a service of
the same name, on the /xyz/openbmc_project/state/bmc0 object path. This
interface provides the Active vs Passive role property as well as some other
redundancy related properties.

## Startup

On startup, the code will wait for up to six minutes for the sibling BMC's
heartbeat to start, assuming the BMC is present. After that it will determine
its role.

## Role Determination Rules

The current rules for role determination are:

1. If the sibling BMC doesn't have a heartbeat, choose active. It could be the
   sibling isn't even present.
1. If the sibling BMC's position matches this BMC's position, choose passive.
   This is an error case.
1. If the sibling isn't provisioned, choose active.
1. If the sibling is already passive, choose active.
1. If the sibling is already active, choose passive.
1. If the previous role isn't unknown, choose that assuming it wasn't passive
   just due to an error.
1. Finally, if this BMC's position is zero choose active, otherwise passive.

### Cases that require a BMC must be passive

There are some error cases that require that the BMC is passive regardless of
what the sibling is doing. These are:

1. The BMC is not provisioned.
1. The systemd service that maintains the sibling API isn't running. Without
   this service running, the sibling BMC will think this BMC is dead and will
   become active.
1. There is an internal failure during role determination, like an exception.

The passive role in all but the last of these cases can be set before the
heartbeat is even started, and waiting for the sibling won't even need to be
done as it doesn't need that info for full role determination.

## After the role is determined

After the role has been determined, the code will

1. Update the Role property on D-Bus.
1. Start either the obmc-bmc-active.target or obmc-bmc-passive.target systemd
   target.
