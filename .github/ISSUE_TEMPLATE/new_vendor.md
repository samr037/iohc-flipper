---
name: New vendor support
about: Captures for adding a new io-homecontrol manufacturer
labels: enhancement, vendor
---

**Manufacturer + model**

<!-- e.g. Schellenberg, Honeywell, ... -->

**Captured button frame**

Paste a row from `frames_<tick>.csv` for any button press (UP/DOWN/STOP) from
your remote. The byte at payload position 1 is the `vendor` byte we need.

```
<paste here>
```

**Captured pair frame (cmd 0x30)**

If you can capture a rings/PROG burst, paste a single row here with the
encrypted install_key bytes redacted (positions 9..24 of the hex column),
keeping `man_id` at position 25 and the trailing `?? + seq` bytes intact.

```
<paste here>
```

**Notes**

Anything else about the remote/motor that might matter (number of product
type codes used, post-pair confirmation gesture, etc.).
