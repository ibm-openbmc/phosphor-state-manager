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

If the BMCs happen to get to determining the role at the same time, the BMC that
was previously passive will wait for the other BMC to determine its role first
to lessen the likelihood of a role switch or each them each thinking they should
be active. One example is when BMC 0 was forced to passive before but isn't
anymore and so would default to active based on its position, and BMC 1 was
active last time and would default to just restoring its role. Letting BMC 1 go
first in this case would ensure BMC 0 remains passive.

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

The actual reason the code used to determine the role is saved in the
`RoleReason` field in `/var/lib/phosphor-state-manager/redundant-bmc/data.json`.

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
1. The active BMC will attempt to enable redundancy.

## Enabling Redundancy

One of the requirements for enabling redundancy is that the passive BMC must be
in the `Ready` state. As the roles can be determined before that state is
reached, the active BMC may need to wait for the passive BMC to get there. It
will wait up to ten minutes total for the passive BMC to get to a steady state
(assuming the passive BMC is alive), which would either be `Ready` or
`Quiesced`.

After the passive BMC reaches steady state, it will then check the following
items to see if redundancy can be enabled:

1. The BMC does have the active role.
1. The sibling BMC is present and is alive (has a heartbeat).
1. The sibling is at the Ready state.
1. The sibling BMC has the Passive role.
1. Redundancy hasn't been manually disabled with the D-bus property that does
   so.
1. The sibling BMC has been provisioned.
1. The sibling's 'sibling communication OK' property is true, meaning it is able
   to talk to the active BMC.
1. The firmware versions are the same on the BMCs.

## Scenarios

### Passive BMC goes to the Quiesced state

If redundancy was enabled, it would be disabled. It would take rebooting the
passive BMC before redundancy could possibly be re-enabled.
