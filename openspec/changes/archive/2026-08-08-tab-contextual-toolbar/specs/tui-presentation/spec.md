## ADDED Requirements

### Requirement: Legend advertises only keys with a target
A view's shortcut legend SHALL list a key only when that key has something to act on. A key whose object does not exist at all — `d=dismiss` on Updates while no dismissible request exists — SHALL be omitted from the legend and SHALL remain a no-op if pressed.

A key that has a target but would be refused by a guard SHALL stay listed: the refusal is information the user is entitled to discover, and it SHALL be explained on the shared status line when the key is pressed, never by removing the key. The gate SHALL read a ViewModel fact the view already receives, so `Render()` gains no new work and no new input.

Omitting a key SHALL NOT change the legend's degradation order or its guarantee that the first entry and the last two survive abridgement.

#### Scenario: Dismiss is unlisted with no request
- **WHEN** the Updates view renders with no dismissible request
- **THEN** the legend does not contain `d=dismiss`, and the remaining keys render in their normal order

#### Scenario: Dismiss appears with a request
- **WHEN** the Updates view renders while a dismissible request is present
- **THEN** the legend contains `d=dismiss`

#### Scenario: A refusable key stays discoverable
- **WHEN** a view renders a selection whose verb a guard would refuse
- **THEN** that verb's key is still listed in the legend, and pressing it reports the guard's reason on the status line

#### Scenario: Gating holds at every tested size
- **WHEN** the Updates view renders at 120x32, 100x28 and 80x24, with and without a dismissible request
- **THEN** the legend's key set matches the request's presence at each size, no rendered row exceeds the screen width, and `q=quit` remains present
