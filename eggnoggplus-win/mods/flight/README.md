# Flight Mode

Press **Ctrl+F** during an offline match to toggle flight for the configured
player. While flight is enabled, use that player's normal **left, right, up,
and down** movement controls. Sword attacks and menu input continue to work.

Flight directly controls position, so the player hovers when no direction is
held and can pass through terrain. The framework automatically suspends this
gameplay-affecting mod during online play.

Settings are available under **Options -> Mods -> Flight Mode**:

- `speed`: movement distance per gameplay tick.
- `acceleration`: how quickly flight reaches the configured speed.
- `deceleration`: how quickly flight glides to a stop.
- `controlled_player`: P1 or P2.
- `normalize_diagonal_speed`: prevents diagonal movement from being faster.
