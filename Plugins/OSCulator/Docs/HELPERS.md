# OSCulator — Helper Nodes

Blueprint nodes that ship with the plugin but have nothing to do with OSC or MIDI.

Everything else in OSCulator is about getting a message from the wire to a function.
These are about what you do with the values **after** they arrive. They take plain
Blueprint types, hold no state, touch no transport, and would work identically if you
called them from a keypress — so a node that resolves an array works the same whether
OSC or MIDI filled it.

They live in `OSCulatorCore` and appear under **OSCulator | …** in the node palette.

---

## The problem they exist for

A function's parameter list is the schema, and it is fixed. A **trailing array**
parameter is how a message of varying length gets into the graph — see
[Variable-length messages](SETUP.md#variable-length-messages) in SETUP.md.

Interpreting that array once it arrives is what these nodes are for.

---

## `Resolve Material Parameter Value`

**Category:** OSCulator | Material

Splits a float array into "this is a scalar" or "this is a colour", so **one** event can
drive both `Set Scalar Parameter Value` and `Set Vector Parameter Value` on a material
instance. Without it you write two events, one per value type, and the sender has to
know which is which.

```
                       ┌──────────────────────────────┐
  Value (float array) ─┤ Resolve Material Parameter   ├─ Scalar ──> float Scalar
                       │           Value              ├─ Color  ──> FLinearColor Color
                       └──────────────────────────────┴─ Invalid
```

Three exec pins. Wire `Scalar` into `Set Scalar Parameter Value` and `Color` into
`Set Vector Parameter Value`. `Invalid` only fires on an empty array — an unwired exec
pin is legal, so in practice you wire two and ignore the third.

### What the length means

Nothing else disambiguates a flat list of numbers, so length alone decides:

| Values | Pin | Result | Logs |
| --- | --- | --- | --- |
| 0 | `Invalid` | — | Warning |
| 1 | `Scalar` | `Values[0]` | |
| 2 | `Scalar` | `Values[0]` | Warning |
| 3 | `Color` | `(r, g, b)`, alpha **1.0** | |
| 4 | `Color` | `(r, g, b, a)` | |
| 5+ | `Color` | first four | Warning |

**Alpha defaults to opaque.** A sender that omits alpha means "opaque", never
"invisible" — and the marshaller's own rule of zeroing what was not supplied would give
you a fully transparent colour, which looks like the node did nothing at all.

**Two values is read as a scalar with a stray value after it.** It is neither form, but
it is almost always a sender with one channel too many rather than a real mistake — a
TouchDesigner CHOP emits every channel it has. Same reasoning for five or more. The call
goes ahead and the warning says what was assumed, which is the same bargain
`CheckArgCount` makes with a surplus everywhere else in the plugin.

**Both value outputs are written on every branch.** The pin you did not take is reset
rather than left alone, so it never carries a value from the previous call — a stale
colour on the scalar branch is the kind of bug that only appears on the second message.

### Example

Event `SetMaterialParam(FName ParamName, float Interp, TArray<float> Value)` on an actor
tagged `OSC_laser`:

```
/laser/SetMaterialParam ["Emissive", 0.25, 0.7]           -> Scalar 0.7
/laser/SetMaterialParam ["Tint",     0.25, 1, 0, 0]       -> Color  (1, 0, 0, 1)
/laser/SetMaterialParam ["Tint",     0.25, 1, 0, 0, 0.5]  -> Color  (1, 0, 0, 0.5)
```

`ParamName` is passed straight to the setter and must match the parameter name in the
material instance. `Interp` is yours to use — the node does no tweening, and driving the
interpolation in-project is the intended pattern.

### The float/double pin trap

The input is a **double** array, because a Blueprint "Float" pin *is* a double in UE5. A
single-precision array pin would refuse to connect to your event's array: Blueprint
autocasts scalars between float and double, but not arrays. If you write a helper of
your own that takes a Blueprint float array in C++, declare it `TArray<double>` or the
pin will not connect.

---

## Adding to this file

A node belongs here when it is useful **after** a message has been delivered and does
not care which transport delivered it. A node that talks to a socket, a device or the
registry belongs in [SETUP.md](SETUP.md) or [CONSOLE.md](CONSOLE.md) instead.

Each entry should carry: the node's category, what it is for, the rule it applies, what
it does at the edges, and — where there is one — the trap that cost time to find.
