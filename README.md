### This repo is now archived since I've decided to use MessagePack for my game instead. I'm no longer interested in this.

# NBTx

NBTx is a tag based binary format based on Notch's NBT format, but it has a few modifications to make it more useful outside the Java world:
* Supports unsigned types.
* Little endian only, because nobody uses big endian today.
* Root tag in a .nbtx file may be a list, too.
* Slight renaming of stuff.

For more information about the format, see NBTx.txt

This library is based on [cNBT](https://github.com/chmod222/cNBT). Motivation for developing this comes from the needs for the upcoming game [Novacube](https://novacubegame.net/). Just like the game, it is being developed targeting C11/C17 gcc and clang. MSVC compiler is not supported due to it's poorer optimization and other quirks but, if you insist, the code should compile there with minor modifications.
