# Hero's Shade

Play as the Hero's Shade in Twilight Princess, as a mod for [Dusklight](https://github.com/TwilitRealm/dusklight).

His armour and ghostly body over Link, his golden wolf for the wolf, and his sword and shield in
place of yours. Everything comes from the game itself, nothing is replaced on disk.

## Install

Needs Dusklight 2.0.2 or newer. Put `heros_shade.dusk` in Dusklight's `mods` folder.

## Options

In the Mods window, under Hero's Shade. Everything is on by default.

| Option | What it does |
| --- | --- |
| Be the Hero's Shade | his body in place of Link's |
| In every outfit | or only the outfits you pick |
| Silent | no voice while you are him (the wolf keeps his) |
| Golden wolf | the wolf is his golden wolf |
| His sword | over the Wooden, Ordon and/or Master Sword |
| His shield | over the Ordon, Wooden and/or Hylian Shield |

## Co-op

Only your own screen changes. If you play [Crests of Courage](https://github.com/remiafterdark/crests-of-courage),
pick the Hero's Shade in its Models tab instead so everyone sees him, and don't use both at once.

## Building

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The Dusklight source is fetched on the first configure, or put a checkout next to this folder.
The mod ends up in `build/mods/heros_shade.dusk`.

## License

The code is MIT, see [LICENSE](LICENSE). Twilight Princess and its models belong to Nintendo. None of
the game's files are in this repository or the mod: it uses the ones in your own copy of the game.
