# BipedalHumanoid, revision 1

The family describes a pose, not a source file's bone names. Runtime bindings use
cooked indices and reference-frame corrections; name suggestions are editor-only
and require explicit acceptance. The initial contract is exported as
`animation/bipedal_humanoid.rigfamily`.

`BipedalHumanoid.v1.json` freezes the accepted contract. NBS loads this document
as authoring data; it does not import Engine code. The corpus generator rejects
a changed reference instead of silently changing the meaning of the family ID.

## Reference frames and hierarchy

Canonical transforms are parent-local TRS, producing model-space positions in
metres after inherited scale. Quaternions are normalized XYZW; scales are positive. The initial
reference is the accepted clean humanoid T-pose. Left/right semantics refer to the
character's own left/right. Joint semantic IDs hash `humanoid/<semantic>`.

The root is hips. Required descendants include the spine, neck, head, shoulders,
arms, forearms, hands, upper legs, legs, feet and toes. Finger joints and terminal
end joints are optional. A present optional joint requires its logical parent.
The serialized family is the authoritative list and ordering. Arm and leg chains
are ordered root-to-effector lists, not names discovered at runtime.

Inherited joints cannot be replaced, reparented or placed below an extension.
Extensions may be added as descendants. This deliberately rejects an inserted
twist bone that changes an inherited joint's parent; such a source needs an
offline topology adapter, not a runtime hierarchy guess.

## Pose conversion

Rotations are rest-relative deltas, conjugated by the cooked source/family frame
correction. Translations are rest-relative offsets, expressed in the destination
parent frame and scaled by the cooked bone-length ratio. Zero-length references
use a ratio of one. Scales transfer as multiplicative rest-relative ratios.
This preserves target rest proportions rather than copying source offsets.

Source-only extensions do not enter the canonical pose. Unmapped optional family
joints use canonical rest. Target-only extensions retain target rest unless an
explicit propagation rule writes them. Helpers must never overwrite inherited
joints. Rules are sorted, single-writer, and acyclic. Supported rules copy local
transforms, copy model transforms, or distribute a source's rest-relative rotation.

Exact Skeleton-domain playback bypasses conversion, including helper policy: its
own tracks remain authoritative. Family-domain clips skip source-to-family work.
Target IK runs after retargeting and target-local composition.

The baseline tier is FK. The optional graph setting `retargetChainCorrection`
adds one bounded two-bone correction for each of the first four authored
three-joint chains. It preserves the proportion-scaled effector displacement
from rest. It is not a foot-plant solver and does not replace target IKRig work.

## Composition boundary

The CPU reference player accepts at most four declared source domains per set
(or the graph's smaller `maximumPoseDomains` limit). This is deliberately more
conservative than allowing an arbitrary number of dormant domains.

Base samples are grouped and blended in their source domain. Layers marked
`composeInSourceDomain` belong to that domain and contribute only while its
base is active. Each resulting domain pose converts once. Independently timed
target-local layers/actions must use distinct non-native domains; binding rejects
reuse that would require another conversion. Native target poses need no conversion.
Cross-domain masks resolve through cooked family indices, not positional matching.

Unrelated schemas require an explicit RetargetProfile. There is no automatic
cross-family guess. Family and profile handle generations are checked before
reusing a cached pose. Unvalidated retarget combinations use the conservative
whole-animation bounds, never a source clip's tighter bounds.

## Acceptance evidence

The clean corpus contains a reference rig, a proportion variant, and a renamed
rig with changed bone rolls and an additional head extension. The Blender corpus
scripts record the input hash and accepted semantic mappings. Game tests validate
the exported bytes and compare every idle, walk and run sample against a separately baked
Blender world-frame reference. Model-space position tolerance is 0.02 mm; quaternion
absolute-dot error tolerance is 0.000002. These numerical checks complement, but
do not replace, visual inspection of the rendered characters.

The accepted corpus is `phase18-accepted`. Earlier `phase18-final` and
`phase18-tall` artifacts are superseded: their generator scaled connected bone
endpoints more than once. Capturing all endpoints before editing fixes the
mesh/rest-rig mismatch. The generator now asserts every transformed endpoint,
and `inspect_retarget_source.py` renders the independently baked pose on the
actual Blender mesh. A passing joint comparison alone is not visual acceptance.
The intermediate `phase18-corrected` corpus repaired that geometry but still
mislabelled raw walk/run tracks. FBXs with the same names and topology can have
different rest frames. Every source is now converted offline into the reference
rig before its tracks are published as reference or canonical-family clips.
