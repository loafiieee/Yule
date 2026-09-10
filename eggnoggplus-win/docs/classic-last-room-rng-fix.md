# Classic last-room RNG divergence

The September 6 LAN reproduction supplied paired trace records for pair
`9D6036F5`, tick 2190, post-tick boundary 2191. Both clients used matching
build/executable/DLL fingerprints. This run recovered through correction and
completed normally; the earlier public match failed correction replay.

Applying the production checksum-only cosmetic masks to both traces reproduces
checksums 1282903789 and 3372505931. The only remaining byte differences are
header offsets 36..39 (`rng_seed`). Players, entities, tile gameplay data,
transient state, and every other checksummed field match.

Both RNG traces start the tick at 672121905. Both call gameplay RNG at return
addresses 0x42CA16 and 0x42CA2C. Only player 1 additionally calls `frnd` at
0x424410. The native RNG recurrence reproduces the exact observed sequence:

1. 248673437
2. 1129937819 (host post-tick seed)
3. 2541194916 (joining client post-tick seed)

Disassembly places the extra call at 0x42440B inside an alternate branch of
`player_update_movement`'s creepy sound effect. The branch uses the pointer
returned by `sound_creepy`, writes pitch fields +0x70/+0x74, and rejoins the
existing sound-only block at 0x42412B. It does not modify the player or map.
The sibling pitch branch was already classified as cosmetic by the
0x4240FF..0x424194 return-address range. The missed alternate call now receives
an exact 0x424410 exception. The existing frnd hook swaps to the private cosmetic
seed and restores gameplay RNG after the sound draw.

The guarded core runner extracts and compiles the production classifier. Its
regression checks both pitch branches, the common tail, exact-call boundaries,
and nearby gameplay calls. No whole movement-function range is excluded.

Live acceptance: use the rebuilt DLL on both peers, enable
`ggpo.net rngtrace on`, and repeat Classic room-1 traversals with both sound
branches. Confirm no new divergence/correction at that effect. Keep both
clients' logs and desync captures if any other failure occurs.

User confirmation: September 7, the updated build resolved the reported Classic
last-room failure. Broader netcode acceptance remains tracked separately.
