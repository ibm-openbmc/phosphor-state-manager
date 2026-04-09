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

### Holding off BMC Ready

The phosphor-wait-for-active-passive-target.service prevents the BMC from
reaching the Ready state until the initial role determination and redundancy
work is complete. This service runs a script that:

1. Polls the Role D-Bus property until it becomes Active or Passive (max 20
   minutes)
2. Waits for the corresponding systemd target (obmc-bmc-active.target or
   obmc-bmc-passive.target) to complete (max 60 minutes)
3. On the active BMC only, waits for the /run/openbmc/bmc_redundancy_determined
   marker file to be created, indicating redundancy determination is complete
   (max 15 minutes)

## Role Determination Rules

The current rules for role determination are:

1. If the sibling BMC doesn't have a heartbeat, choose active. It could be the
   sibling isn't even present.
1. If the sibling isn't provisioned, choose active.
1. If the sibling is already passive, choose active.
1. If the sibling is already active, choose passive.
1. If there was previously a failover in progress, choose active. See
   [below for more details](#reboots-in-the-middle-of-a-failover).
1. If the sibling has a failover in progress, choose passive.
1. If the previous role isn't unknown, choose that assuming it wasn't passive
   just due to an error.
1. Finally, if this BMC's position is zero choose active, otherwise passive.

The actual reason the code used to determine the role is saved in the
`RoleReason` field in `/var/lib/phosphor-state-manager/redundant-bmc/data.json`.

### Cases that require a BMC must be passive

There are some error cases that require that the BMC is passive regardless of
what the sibling is doing. These are:

1. The BMC is not provisioned.
1. The BMC position cannot be determined.
1. System inventory processing failed.
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

There is also a ten minute wait for the network connection between the BMCs.
This is in parallel with the steady state wait so if one is complete first it
will still wait for the other.

After the passive BMC reaches steady state, it will then check the following
items to see if redundancy can be enabled:

1. The BMC does have the active role.
1. The sibling BMC is present and is alive (has a heartbeat).
1. The sibling is at the Ready state.
1. The sibling BMC has the Passive role.
1. If the sibling BMC indicates that it can never be active.
1. Redundancy hasn't been manually disabled with the D-bus property that does
   so.
1. The sibling BMC has been provisioned.
1. The firmware versions are the same on the BMCs.
1. The network between the BMCs is connected.
1. If attempting to enable any time at runtime, redundancy must have been
   enabled when runtime was first reached.

## Scenarios

### Passive BMC goes to the Quiesced state

If redundancy was enabled, it would be disabled. It would take rebooting the
passive BMC before redundancy could possibly be re-enabled.

### Passive BMC heartbeat changes

The passive BMC's heartbeat could be lost due to events like:

- The passive BMC is rebooted
- The passive BMC dies
- A cable is pulled
- The RBMC management application on the passive BMC dies

When the passive heartbeat stops, redundancy is functionally disabled as there
is no passive BMC alive to handle the failover method. The active BMC will not
do anything for five minutes to allow time for the passive BMC to come back
after a reboot. At five minutes, it is assumed that the BMC won't come back and
RedundancyEnabled will be set to false and an event log will be created.

The active BMC will always notice when the passive BMC's heartbeat starts,
either after a recovery or for the first time if added late. It will wait for
the passive BMC to assume the passive role and then do the same checks as on
startup to see if redundancy can be enabled. If redundancy can be enabled, a
full sync will be done to handle any files that changed when the passive BMC
wasn't running.

Note that redundancy cannot be enabled at runtime if the system wasn't booted
with redundancy enabled. A concurrent maintenance operation would be necessary
in that case.

### Passive BMC loses network connection

A loss of the network connection between the BMCs will start a five minute timer
similar to how it was done for a heartbeat loss. At five minutes, redundancy
will be disabled and an event log will be created. If it comes back before then,
it will go through the code to calculate redundancy followed by a full sync to
sync over any files that might have changed when the network was down.

The heartbeat timer will take precedence over this one. So if the heartbeat
isn't currently active when the network loss occurs, the new timer won't be
started since the overall loss of the passive BMC is already being handled.

In addition, if the network loss timer is already running when the heartbeat
loss is noticed, it will be canceled when the heartbeat loss timer is started.

## Interacting with Data Sync on the Active BMC

### When Enabling Redundancy

Any time redundancy is being enabled on the active BMC, or when the active BMC
detects that the passive BMC recovered from some interruption even if redundancy
wasn't disabled on D-Bus yet, the following steps will be taken:

1. Set the `DisableSync` property to false.
1. Call `StartFullSync` to start the full sync.
1. Wait for the `FullSyncStatus` property change to `FullSyncCompleted` or
   `FullSyncFailed`.

If the full sync fails, redundancy will be disabled and `DisableSync` will be
set to true. Note that if the system ever goes through a transition that causes
another attempt at enabling redundancy, it won't be prevented and the full sync
will be attempted again.

### When disabling redundancy

Whenever the active BMC attempts to enable redundancy and it can't for some
reason, it will set the `DisableSync` property to true to stop background
syncing if it was occurring.

### Background Sync errors

The `SyncEventsHealth` property says if any background sync operations had
failures. Since retries are built into the sync daemon, it should take something
serious for the health property to change to critical, such as:

- rsync daemon on either BMC crashing
- passive BMC reboot
- loss of network connectivity

Depending on when a sync happens to occur in relation to the root cause of the
fail, the property change may be the first indication that something is wrong,
or it may not change until quite some time later if at all.

To deal with all of this, code will watch for the health property to change to
critical, and then delay for 5 seconds to allow a sibling heartbeat loss to get
noticed by the BMC.

After the 5 seconds, if the sibling doesn't have a heartbeat then the code will
just let the sibling heartbeat monitoring code handle the situation.

However if the sibling still does have a heartbeat, then redundancy will be
disabled with the reason being a sync failure.

## Interacting with the sync daemon on the Passive BMC

Similar to the active BMC, the passive BMC also has to issue a full sync.

It will start the full sync, which also starts the background sync, using the
same sequence as the active BMC when:

- The `RedundancyEnabled` property from the active BMC changes to true
- The active BMC heartbeat starts:
  - If redundancy is not enabled at this point, nothing will happen.

Background sync will be stopped when:

- The `RedundancyEnabled` property changes to false
- The active BMC heartbeat stops.
- The `SyncEventsHealth` property changes to critical.

If background sync hits a failure and the health changes to critical, redundancy
will not be disabled. The sync will explicitly stopped and then it would just be
restarted next time a full sync is done. It will do the same 5 second wait the
active BMC does to see if the heartbeat has stopped, meaning most likely the
active BMC either died or was rebooted.

## Failovers

A failover is when redundancy is enabled and the passive BMC takes over as the
active BMC, and the original active BMC becomes passive. Redundancy may or may
not be re-enabled afterwards, depending on if something is preventing it or not.

The failover is always driven by the original passive BMC, which then takes over
as active.

### Allowing Failovers

Even when redundancy is enabled, there are periods when failovers will not be
allowed. The `FailoversAllowed` D-Bus project reflects this state.

Failovers aren't allowed when:

1. Redundancy is disabled.
2. The system is at some state other than off or runtime.
3. `RedundancyEnabled` has changed to true but a full sync hasn't been
   completed.
4. A failover is in progress.
5. More coming.

When failovers aren't allowed, rbmctool can be used to display the reasons why.

Future work to be done:

- Put the reasons on D-Bus and in Redfish so the HMC can get them.
- Determine if the other BMC needs the reasons, or just if FOs aren't allowed.
- When writing the failover code, reject the failover if it isn't allowed,
  though there still needs to be a method to force it for use by field support.

### Rejecting a failover request

When the call is made to start the failover on the passive BMC, it will reject
the request if any of the following are true.

1. A failover is imminent or already in progress.
1. Redundancy isn't enabled.
1. FailoversAllowed is false. Exceptions are:
   - The `force` option was passed into the `StartFailover` method.
   - The active BMC is in the `Quiesced` state.
1. A full sync on the passive BMC is in progress.
1. The active BMC has no heartbeat and redundancy wasn't last known to be
   enabled. If it was last known to be enabled, a failover is allowed so that
   the remaining BMC can become active.
1. The passive BMC is not in the `Ready` state.

### Failover Sequence

The failover sequence is:

1. The `StartFailover` D-Bus method is called on the passive BMC daemon.
1. The passive BMC checks to make sure
   [it doesn't need to reject the request](#rejecting-a-failover-request). If it
   does an error is returned via D-Bus.
1. The passive BMC disables background syncs.
1. The passive BMC sets its `FailoverImminent` property to true, and then waits
   for 10 seconds to allow the other BMC to prepare for the failover assuming it
   is alive.
1. The passive BMC clears `FailoverImment` and sets `FailoverInProgress`.
1. The passive BMC toggles the reset on the sibling BMC to reboot it.
1. The passive BMC now switches its role to Active.
1. The new active BMC sets `FailoversAllowed` to false.
1. The new active BMC takes over the hardware access bus.
1. The new active BMC starts obmc-bmc-active.target. This starts all of the
   active only services. It will not proceed until all active services have been
   started which may take some time.
1. The new active BMC waits for the sibling BMC to restart its heartbeat and get
   to the BMC ready (or quiesced) state.
1. The new active BMC clears `FailoverInProgress`.
1. The new active BMC evaluates if redundancy can still be enabled, and if so it
   issues a full sync. Otherwise it sets `RedundancyEnabled` to false.
1. When the full sync is complete, `FailoversAllowed` is now set to true.

### Failover History

The BMC driving the failover logs the requester and timestamp of each failover
in the file `/var/lib/phosphor-state-manager/redundant-bmc/bmcN_failovers`,
where `N` is the BMC position.

The most recent 10 failovers are kept.

### Reboots in the middle of a failover

If the new active BMC is rebooted in the middle of a failover while the other
BMC is somewhere in its reboot, the roles should be preserved with the new
active BMC staying active.

Preserving the roles is accomplished by:

- On the new active BMC at the start of the failover save the failover in
  progress indication in the filesystem. On every startup, look for that
  indication and use it in the
  [role determination calculation](#role-determination-rules). It will be
  cleared from the filesystem after a successful failover or after it was used
  in role determination.

- On the new passive BMC during role determination obtain the failover in
  progress value from the other BMC. If it is true, then choose the passive
  role.

### Active BMC handling of an imminent failover

When the current active BMC sees the failover imminent indication, it will do
the following to prepare to be reset and come back as the passive BMC:

1. Stop background syncing.
2. Flush any unwritten journal messages to disk.
