# calendar — Developer Context

## What This Module Does
Advances in-game date, tracks deadlines and scheduled events, emits
consequence deltas for expired deadlines. Global (not province-parallel).

## Tier: 1

## Key Dependencies
- runs_after: []
- runs_before: ["scene_cards"]
- Reads: CalendarEntry list, current_tick, calendar state
- Writes: ConsequenceDelta for expired deadlines, updated calendar state

## Critical Rules
- Calendar starts January 1, 2000
- One tick = one in-game day
- Deadline expiration triggers consequences via DeferredWorkQueue
- Seasonal flags (summer/winter/etc.) derived from date and province latitude

## Interface Spec
- docs/interfaces/calendar/INTERFACE.md
