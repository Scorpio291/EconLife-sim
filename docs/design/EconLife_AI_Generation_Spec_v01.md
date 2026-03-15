# EconLife — AI Generation Specification
*Version 0.1 — Companion document to GDD v1.7, TDD v29, Scene Card Rulebook v0.1*
*Cross-reference: TDD §9 (SceneCard struct, SceneSetting enum), GDD §20 (UI/UX Philosophy), Scene Card Rulebook §5–6 (NPC Presentation State, Visual Style)*

---

> **Document Purpose**
> EconLife uses AI generation for all scene card illustrations, NPC portraits, NPC appearance and backstory, music, and sound design. This document specifies the complete generation pipeline: the asset taxonomy (what gets generated and when), the NPC Randomizer system (how every NPC gets a unique appearance and backstory blurb at world-gen time), the Claude Prompt Builder (the system prompt and I/O schemas Claude uses to construct generation prompts), and the per-modality prompt rules for image, music, and sound.
>
> **Architecture:** All AI generation is offline — no API calls during gameplay. Two offline pipelines exist: a **world-gen pipeline** that runs when a new world.json is finalized (generating NPC appearance descriptions, backstory blurbs, and portraits), and a **pre-generation pipeline** that runs ahead of release (generating all scene backgrounds, music, and sounds). The game engine reads from a pre-populated asset library. The only "runtime" operation is texture compositing — blending a pre-generated background with a pre-generated NPC portrait at scene card display time. This is a rendering operation, not AI generation.

---

## 1. Pipeline Architecture

```
┌────────────────────────────────────────────────────────────┐
│  WORLD-GEN PIPELINE  (runs once per world.json)            │
│                                                            │
│  Input: world.json — all provinces, demographics,          │
│         Significant NPCs with role + motivation data       │
│                                                            │
│  Step 1 — NPC Randomizer (§2.3)                           │
│    For each Significant NPC:                               │
│    a. Generate appearance description (locked string)      │
│       from province demographics + role + age.             │
│    b. Generate backstory blurb (2–3 sentences)             │
│       from province context + role + dominant motivation.  │
│    c. Store both in NPCPortraitProfile.                    │
│                                                            │
│  Step 2 — NPC Portrait Generation                          │
│    Claude converts each appearance description into        │
│    3 portrait image prompts (cold / neutral / warm).       │
│    Prompts dispatched to image API.                        │
│    Portraits stored: npc_{id}_portrait_{tier}.{ext}        │
│                                                            │
│  Step 3 — Quality Review                                   │
│    NPC portraits reviewed against checklist (§9).          │
│    Failures re-generated with adjusted prompts.            │
└────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│  PRE-GENERATION PIPELINE  (runs ahead of release)          │
│                                                            │
│  Step 1 — Asset Enumeration                                │
│    Generate the full matrix of required assets from        │
│    the combinatorial space (§2.1).                         │
│                                                            │
│  Step 2 — Claude Prompt Builder (§3)                       │
│    For each required asset: pass a JSON context object     │
│    to Claude → Claude returns a JSON prompt package        │
│    (image / music / sound).                                │
│                                                            │
│  Step 3 — Generation API Execution                         │
│    Prompts dispatched to image, music, and sound APIs.     │
│    Results saved to asset library with canonical names.    │
│                                                            │
│  Step 4 — Quality Review                                   │
│    Assets reviewed against checklist (§9).                 │
│    Failures re-generated with adjusted prompts.            │
└────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│  GAMEPLAY  (no AI generation)                              │
│                                                            │
│  The game engine reads from the pre-populated asset        │
│  library. Scene cards are rendered by compositing:         │
│    background image  (pre-generated, keyed to setting      │
│                        + presentation_tier + era + time)   │
│  + NPC portrait       (pre-generated, keyed to npc_id      │
│                        + presentation_tier)                │
│  + dialogue panel     (typographic UI overlay)             │
│  + choice buttons     (typographic UI overlay)             │
│                                                            │
│  Compositing is a rendering operation — no API calls.      │
│  Music and sound are loaded directly from pre-gen tracks.  │
└────────────────────────────────────────────────────────────┘
```

---

## 2. Asset Taxonomy

### 2.1 What Gets Pre-Generated

The combinatorial space for scene card backgrounds is:

| Dimension | Values | Notes |
|---|---|---|
| SceneSetting | 27 | Full enum from TDD §9 |
| Presentation tier | 3 | Maps npc_presentation_state to three visual registers: cold (0.0–0.33), neutral (0.34–0.66), warm (0.67–1.0) |
| Era | 5 | era_1 through era_5 (V1 scope) |
| Time of day | 2 | Daytime / evening–night |
| **Total backgrounds** | **810** | 27 × 3 × 5 × 2 |

Not every combination is equally important. The authoring priority tiers from the Scene Card Rulebook (§6) apply here: Priority 1 settings (path-opening, hostile action, death) receive fully curated prompts; Priority 2 settings receive curated prompts with era/state variants; Priority 3 settings receive templated prompts with injected variables.

**Music tracks:** 1 track per (era × tension_tier × setting_category) combination. See §5.1 for the taxonomy.

**Sound — ambient loops:** 1 ambient soundscape per SceneSetting = 27 loops.

**Sound — event sounds:** ~40 discrete UI and narrative event sounds (see §6.3).

**NPC portraits:** Generated per-NPC at world-gen time. Not part of the offline pre-generation run.

### 2.2 Scene Card Compositing

Scene cards are assembled by the game engine at display time from pre-generated components. This is texture compositing — no AI generation occurs:

```
background image (setting × presentation_tier × era × time_of_day)
  + NPC portrait (npc_id × presentation_tier)  [positioned per setting convention]
  + dialogue panel (typographic UI overlay)
  + choice buttons (typographic UI overlay)
= rendered scene card
```

The background and portrait are both painted-style images. The dialogue panel and choice buttons are clean typographic UI — maintaining the painted/typographic contrast specified in Scene Card Rulebook §6. NPC portrait position within the background is determined by the setting's composition convention (see §4.1, "Composition notes" column).

### 2.3 NPC Assets (World-Gen Pipeline)

Every Significant NPC in the world requires three AI-generated assets, all produced during the world-gen pipeline:

| Asset | Format | Pipeline step |
|---|---|---|
| Appearance description | Locked string stored in `NPCPortraitProfile.character_description` | NPC Randomizer (§2.4) |
| Backstory blurb | 2–3 sentence string stored in `NPCPortraitProfile.backstory_blurb` | NPC Randomizer (§2.4) |
| Portrait — cold tier | Image file: `npc_{id}_portrait_cold.{ext}` | Portrait generation from appearance description |
| Portrait — neutral tier | Image file: `npc_{id}_portrait_neutral.{ext}` | Portrait generation from appearance description |
| Portrait — warm tier | Image file: `npc_{id}_portrait_warm.{ext}` | Portrait generation from appearance description |

**Scale:** 500–1,000 Significant NPCs per region × 3 portrait variants = 1,500–3,000 portrait images per world. At typical image generation speeds this is manageable as a batch job run once per world.json finalization.

**No aging variants in V1.** Portraits do not update as NPCs age within a playthrough. A visual age-update system (generating a new portrait when an NPC has aged 15+ in-game years) is deferred to post-launch tooling once the base pipeline is stable.

### 2.4 NPC Randomizer

The NPC Randomizer runs as Step 1 of the world-gen pipeline. It takes each Significant NPC's role, province context, and motivation data and produces the two locked text fields that drive all downstream generation for that NPC.

**Inputs per NPC:**

```json
{
  "npc_id": 4821,
  "npc_role": "regulator",
  "npc_age_years": 47,
  "npc_motivations": {
    "career_advancement": 0.6,
    "ideology": 0.3,
    "stability": 0.1
  },
  "province_context": {
    "province_name": "Hartwell County",
    "dominant_industry": "manufacturing",
    "wealth_tier": "mid",
    "climate_descriptor": "temperate, four seasons",
    "urban_density": "mid-sized city"
  }
}
```

**Output per NPC (stored in NPCPortraitProfile):**

```json
{
  "character_description": "<locked appearance string — see §2.4.1>",
  "backstory_blurb": "<2–3 sentence backstory — see §2.4.2>"
}
```

#### 2.4.1 Appearance Description Generation

Claude generates the `character_description` using the following prompt:

---

```
Generate a locked appearance description for an NPC character. This description will be used verbatim in every image generation prompt for this character — it must be visually consistent, specific enough to produce a stable identity across multiple generations, and not so specific that it constrains the artist unnecessarily.

NPC data:
- Role: {npc_role}
- Age: {npc_age_years} years old
- Province: {province_name} — dominant industry: {dominant_industry}, wealth tier: {wealth_tier}, climate: {climate_descriptor}
- Dominant motivation: {dominant_motivation_label}

Rules:
1. Describe apparent age as a range (±5 years around the actual age).
2. Describe gender expression as it presents visually — avoid binary assumptions. Use descriptive language ("presents as masculine", "androgynous", "feminine presentation") rather than assigning a label.
3. Include ONE or TWO visually distinctive and stable features — something that will read consistently across cold, neutral, and warm portrait variants. Examples: a particular hair colour and cut, glasses, a scar or mark, a build.
4. Describe default clothing for their role and economic position. Match to the province's wealth tier.
5. Include ONE body language note that reflects their dominant motivation. This will inform portrait posture.
6. Maximum 4 sentences. Plain, matter-of-fact language. No dramatic adjectives.

Output: the character_description string only. No other text.
```

---

**Example outputs:**

*`regulator`, 47, manufacturing province, career_advancement dominant:*
> "Woman in her late forties, South Asian heritage, medium build. Dark hair worn in a practical bun; reading glasses pushed up on her forehead. Business-casual — blazer over a collared shirt, muted colours; the kind of wardrobe that communicates competence rather than ambition. She carries herself with the quiet authority of someone who is usually the most prepared person in the room."

*`criminal_operator`, 34, coastal province, survival dominant:*
> "Man in his early-to-mid thirties, mixed Black and Latino heritage, lean but physically capable. Close-cropped hair; a faded scar through his left eyebrow. Practical clothing — dark jeans, a plain work jacket — nothing that stands out. His posture is watchful: weight slightly forward, never fully at ease in an open space."

*`journalist`, 29, financial district province, ideology dominant:*
> "Non-binary presenting, late twenties, East Asian heritage, slight build. Short hair, worn practical. Wire-rimmed glasses. Usually in a plain button-down, sleeves rolled up — cheap but cared-for. Leans forward when listening, as though everything said might matter."

#### 2.4.2 Backstory Blurb Generation

Claude generates the `backstory_blurb` using the following prompt:

---

```
Generate a 2–3 sentence backstory blurb for an NPC. This text appears as flavour in the NPC's profile panel when the player inspects them. It is not mechanically used — it exists to make the NPC feel like a real person with a history.

NPC data:
- Role: {npc_role}
- Age: {npc_age_years} years old
- Province: {province_name} — dominant industry: {dominant_industry}, wealth tier: {wealth_tier}, climate: {climate_descriptor}, urban density: {urban_density}
- Dominant motivation: {dominant_motivation_label}

Rules:
1. Structure: past → present. Where did they come from, what shaped them professionally, what is currently true about their situation.
2. Plain, matter-of-fact tone. No dramatic language. This is a real person.
3. Do not name the NPC. Do not use "they/he/she" — write in the third person descriptive ("Grew up in...", "Has worked...", "Runs a...").
4. Ground the blurb in the province's dominant industry and wealth tier. The world they grew up in should be recognisable.
5. Reflect the dominant motivation subtly — not explicitly ("their ambition drives them") but through the choices described.
6. 2–3 short sentences. Maximum 60 words total.

Output: the backstory_blurb string only. No other text.
```

---

**Example outputs by role and motivation:**

*`regulator`, career_advancement, mid-wealth manufacturing province:*
> "Came up through the environmental compliance side of the agency in a region where that meant something. Made regulatory director faster than most because she was willing to hold the line when it was politically easier not to. Has a reputation for fairness that she considers an asset and some of her colleagues consider a constraint."

*`criminal_operator`, survival, low-wealth coastal province:*
> "Grew up in the harbour district when it was still a working waterfront. Got into distribution in his mid-twenties because the fishing industry had already gone. Runs a tight operation and doesn't expand because he knows what expansion costs."

*`journalist`, ideology, high-wealth financial district province:*
> "Turned down three private-sector offers to stay in local news. Covers financial irregularities and political influence with the patience of someone who believes it eventually matters. Lives in a flat she can barely afford two blocks from the paper."

*`union_organizer`, ideology, mid-wealth industrial province:*
> "Third-generation factory worker from the east side of the city. Got active in her early twenties after a workplace accident that the company settled quietly. Has been organising in the same sector for eight years and knows everyone worth knowing on the floor."

*`politician`, power, high-wealth urban province:*
> "Was a city council member before most of his current staff were in secondary school. Built a base through neighbourhood infrastructure work that nobody else wanted to take credit for. Understands exactly what he owes and to whom, and keeps a very accurate mental ledger."

*`lawyer`, money, mid-wealth province:*
> "Studied at the state university on loans she finished paying off at thirty-four. Built a criminal defence practice from walk-in clients and worked up to the cases that matter. Charges what the client can pay and writes off more than her partners know."

---

## 3. The Claude Prompt Builder

Claude is invoked once per asset via the offline pipeline. It receives a JSON context object and returns a JSON prompt package. The pipeline tool is deterministic: the same context object always produces the same prompt package (Claude temperature = 0 for all prompt-builder invocations).

### 3.1 System Prompt

The following system prompt is used for all Claude prompt-builder invocations. It is the invariant instruction set — the context object (§3.2) provides the asset-specific variables.

---

```
You are the prompt architect for EconLife, an agent-based economic simulation game.

Your job is to generate precise prompts for AI image, music, and sound generation. You receive a structured JSON context describing an asset that needs to be generated. You return a structured JSON prompt package.

STYLE INVARIANTS — these rules apply to every image prompt you generate, without exception:

VISUAL STYLE:
The EconLife scene card visual style is: oil painting, gouache, visible brushwork, painterly, atmospheric. Not photorealistic. Not digital illustration. Not anime or cartoon. The style should read at screen scale before close inspection — emotional content is legible at a glance. Fine detail is a reward for looking, not a requirement for understanding. The roughness of the brushwork is intentional and consistent — it should feel inhabited, not unfinished.

COMPOSITION:
Scene cards are landscape-orientation illustrations. The primary NPC (if present) occupies left or right foreground — never centered. Secondary figures (if present) are mid-ground or background. Maximum three figures in any scene. Crowd scenes are environmental — no individual portraits. The lower third of the image is reserved for the dialogue overlay panel; it should contain nothing compositionally critical and can be atmospheric/dark so text reads over it.

SETTING PRIMACY:
The setting is not a backdrop — it is information. The same setting should feel different at different emotional registers (presentation_tier) through palette and lighting direction, not through recomposition. Warm palette, softer light, relaxed composition = high presentation_tier. Cool palette, harder shadows, tighter framing = low presentation_tier.

ERA ACCURACY:
Era-specific visual details must be accurate. Technology visible in backgrounds (computers, phones, vehicles, signage) must match the era. Clothing and fashion must be period-accurate. Era 1 (2000–2007) is pre-smartphone — no smartphones are visible. Era 2 (2007–2013) introduces early smartphones but they are not universal. Era 3+ shows progressive technology saturation. Specific era details are in the context object.

NEGATIVE PROMPT DEFAULTS (include in every image prompt):
photorealistic, hyperrealistic, 3D render, CGI, digital painting (clean), anime, cartoon, flat illustration, minimalist, vector, text, watermark, logo, nsfw, violence, blood, weapons visible

MUSIC PROMPTS:
EconLife music is non-diegetic. It sets atmosphere without being intrusive. Avoid lyrics. Avoid genre clichés that feel game-like (no cinematic orchestral swells, no "epic" music). The music should feel like something the world might actually produce — jazz, ambient, post-bop, early electronic, contemporary classical, world music — filtered through the era and emotional register. Instrumentation should be sparse to moderate; the simulation is already complex and the music should add texture, not compete.

SOUND PROMPTS:
Ambient sounds are environmental texture. They are not dramatic — they are the background hum of the space. For remote communication settings (phone_call, video_call), the ambient is the audio texture of a voice call — line presence, subtle background noise from the other end. Sound prompts should describe the actual physical environment and its sounds, not a narrative interpretation.

CONSISTENCY:
NPC portrait prompts must include the NPC's locked character description verbatim (provided in the context object under npc_portrait_profile.character_description). Do not deviate from the character description — consistency across all scene cards featuring this NPC depends on it.

OUTPUT FORMAT:
Return a JSON object with the following structure. Do not include any text outside the JSON object.

{
  "asset_id": "<string from context>",
  "asset_type": "background_image" | "npc_portrait" | "music_track" | "ambient_sound" | "event_sound",
  "image_prompt": "<string, present only for background_image and npc_portrait>",
  "negative_prompt": "<string, present only for background_image and npc_portrait>",
  "music_prompt": "<string, present only for music_track>",
  "sound_prompt": "<string, present only for ambient_sound and event_sound>",
  "generation_notes": "<string — any notes for the human reviewer about this asset>",
  "style_anchor_check": "<string — confirm the style invariants are present: list which are applied>"
}
```

---

### 3.2 Input Schema

The context JSON object passed to Claude for each asset type:

**Background image context:**

```json
{
  "asset_id": "bg_{setting}_{presentation_tier}_{era}_{time_of_day}",
  "asset_type": "background_image",
  "setting": "parking_garage",
  "setting_description": "Classic discreet meeting location. Poor lighting, private, physically exposed.",
  "presentation_tier": "cold",
  "presentation_tier_range": "0.0–0.33",
  "era": "era_1",
  "era_label": "Turn of the Millennium (2000–2007)",
  "era_visual_notes": "No smartphones. Computers are boxy CRTs or early flat-panel LCDs. Vehicles are late 1990s–early 2000s models. Phones are clamshell or bar form factor. CCTV cameras are present but not ubiquitous. Fluorescent lighting dominant in commercial spaces.",
  "time_of_day": "evening_night",
  "npc_present": true,
  "npc_position": "right_foreground",
  "authoring_tier": 2
}
```

**NPC portrait context:**

```json
{
  "asset_id": "npc_{npc_id}_portrait",
  "asset_type": "npc_portrait",
  "npc_id": 4821,
  "npc_portrait_profile": {
    "character_description": "Woman, late 40s, South Asian heritage, medium build. Dark hair worn in a practical bun. Sharp, observant eyes with reading glasses pushed up on her forehead. Usually in business-casual clothing — blazer over a collared shirt, muted colours. She carries herself with the quiet authority of someone used to being right and not bothered whether people know it.",
    "role": "regulator",
    "home_province_id": 3,
    "presentation_variants": ["cold", "neutral", "warm"]
  },
  "presentation_tier": "neutral",
  "era": "era_2"
}
```

**Music track context:**

```json
{
  "asset_id": "music_{era}_{tension_tier}_{setting_category}",
  "asset_type": "music_track",
  "era": "era_1",
  "era_label": "Turn of the Millennium (2000–2007)",
  "era_music_palette": "Late 1990s and early 2000s aesthetic. Downtempo electronica, trip-hop, post-bop jazz. Y2K-era corporate ambience. Early laptop music. Think: Portishead, Kruder & Dorfmeister, late Miles Davis influence, Nils Frahm precursors.",
  "tension_tier": "dominance",
  "tension_description": "Strategic competition. The player has resources; the question is how to deploy them. Confident but not triumphant. The pressure is real but manageable.",
  "setting_category": "business_interior",
  "duration_seconds": 180,
  "loop": true
}
```

**Ambient sound context:**

```json
{
  "asset_id": "ambient_{setting}",
  "asset_type": "ambient_sound",
  "setting": "factory_floor",
  "setting_description": "Industrial facility operations area. Loud, physical.",
  "era": "era_1",
  "duration_seconds": 60,
  "loop": true
}
```

**Event sound context:**

```json
{
  "asset_id": "event_sound_{event_type}",
  "asset_type": "event_sound",
  "event_type": "mandatory_card_arrive",
  "event_description": "A high-priority event is arriving. The player must attend to it. The sound should convey urgency without alarm — important, not panicked.",
  "duration_ms": 800
}
```

---

## 4. Scene Card Background Image Prompts

### 4.1 Setting Prompt Library

For each of the 27 `SceneSetting` values, the following table provides the Claude prompt builder with setting-specific image guidance. These are injected into the context object's `setting_description` field and supplement the TDD's brief annotations.

| Setting | Core visual character | Composition notes | Era-sensitive elements |
|---|---|---|---|
| `boardroom` | Formal, long table, power geometry. Natural light from high windows or a single overhead band. The space implies scrutiny — whoever is in this room is being watched. | NPC at far end of table OR seated across from empty near-end. Camera from entry end, slight low angle. | Computer monitors on table: CRTs (era_1), flat screens (era_2+). Phones on table: corded landlines (era_1–2), cordless (era_2–3). |
| `private_office` | One-on-one. Bookshelves, a desk, the sense that someone works here. Less formal than boardroom — more candid. Afternoon light or desk lamp. | NPC behind or beside desk. Framing close enough to read face clearly. | Desktop computer on desk mandatory era_1–3. Laptop replaces by era_3. |
| `open_plan_office` | Background clutter of other people not paying attention. The conversation is visible. Nothing sensitive should be said here. Fluorescent overheads. | NPC in foreground, colleagues blurred mid-ground. Slight telephoto compression to suggest density. | Cubicle walls with corkboards (era_1–2). Open-plan benching (era_3+). |
| `factory_floor` | Scale and noise implied. Machinery, conveyor belts, hard-hat territory. Worker figures in background. High ceilings. | NPC near foreground machinery or safety barrier. Motion blur on background equipment. | Analog gauges and controls (era_1–2). Digital displays supplement (era_3+). |
| `warehouse` | Liminal. Shelf rows extending to dark. Loading dock light from one direction. Private because empty, not because secured. | NPC half in shadow. Low camera, slight upward angle to emphasize height of shelves. | Palette wrap machines, cardboard, generic — not era-specific. |
| `construction_site` | Outdoor or partially enclosed. Scaffolding, exposed materials, hard sky. Casual because physical — work happens here. | NPC in work gear or business wear that looks wrong for the context. Wide framing. | Construction equipment, vehicles — era-general. |
| `laboratory` | Clinical. Precise surfaces. Either a legitimate research lab (white, ordered) or a clandestine production lab (functional, improvised, materials not quite right). The setting should read as one or the other, not ambiguous. | NPC near equipment or worktable. Background depth through lab glassware and equipment. | CRT monitors for instruments (era_1), then LCD (era_2+). Specific lab context should be specified in card generation if criminal lab vs. research lab. |
| `restaurant` | Semi-public warmth. White tablecloths or casual tables depending on meeting tier. Other diners visible but distant. Window light or candle-warm. | NPC across table from implied player. Low angle to keep background diners soft. | Table-side phones gone by era_2. Smartphones appear on tables by era_3 (face-down etiquette). |
| `cafe` | Casual, daylit, public. The least guarded meeting setting. People come and go. Background noise is presence, not threat. | NPC at small table. Framing relaxed and open. Street or window visible in background. | Espresso machines, baked goods — not era-specific. WiFi signage appears era_2+. |
| `hotel_lobby` | Transitional. People pass through, never belong. Marble or carpet, depends on tier. The deniability of a public space with the privacy of anonymity. | NPC in foreground seating. Background flows with passing figures. | Lobby aesthetics shift by era — era_1 has more physical key cards and paper check-in visible in background. |
| `nightclub` | Dark. Colour from lights, not ambient. Crowd presence is privacy through noise. The meeting that happens here is not meant to be remembered. | NPC close, framing tight. Background is colour and motion, not faces. Low presentation_tier reads as threatening; high reads as conspiratorial-friendly. | DJ equipment, monitors: era_1 has CD decks, era_3+ has laptop setups. |
| `government_office` | Institutional. File cabinets. Government signage. Everything is on-record and both parties know it. Overhead fluorescents. Slightly too small for its purpose. | NPC behind desk, formal. Camera from visitor's seated position — slight upward angle. | Paperwork is heavy (era_1). Computers are present but secondary. |
| `courthouse` | Stone or wood. Gravity of institution. The player is not in their element here. | Exterior steps or corridor — not courtroom. NPC (lawyer, investigator) in foreground. | Not era-specific — institutions change slowly. |
| `courtroom` | Structured and adversarial. The spatial layout communicates power and process — judge elevated, benches facing forward. The player is here because they have to be. | Wide shot showing the spatial hierarchy. NPC (lawyer or judge) in foreground. | Not era-specific. |
| `police_station` | Institutional but lower tier than courthouse. Reception desks, fluorescents, waiting area. The player is here voluntarily or not. | NPC (investigator or officer) in foreground. Institutional background. | Era_1: more paper files visible. Era_3+: more screens. |
| `prison_visiting` | Glass or mesh dividers. Institutional acoustics implied. Both figures are constrained in different ways. | Split composition: visitor and incarcerated figure on either side of barrier. | Not era-specific. |
| `prison_cell` | Small. The operational scope is visible in the physical space — there is nowhere to go. Bunk, toilet, window. | Solo or with cellmate in background. Framing is tight — the constraint is the composition. | Not era-specific. |
| `street_corner` | Urban. Specific but generic — a corner that could be any city. Parked cars, awnings, streetlights. The meeting is visible to anyone who passes. | NPC at corner, slightly facing away from traffic. Wide enough to see the street. | Era_1: no smartphones, payphones may appear. Era_3+: smartphones visible in passers-by hands. |
| `public_park` | Open, leafy, outdoor. The surveillance difficulty is the point — no cameras in the trees. Daylit or dusk. | NPC on bench or path. Camera from path level. Background is nature, not infrastructure. | Not era-specific — parks don't change much. |
| `parking_garage` | Concrete. Poor lighting. The classic discreet meeting. The player is physically exposed — if something goes wrong, there is no cover. | NPC half-lit by a single light source. Concrete columns frame the scene. Camera mid-height. | Vehicles must be era-accurate. Era_1: late 1990s–early 2000s models. No modern vehicles in era_1–2. |
| `political_rally` | Crowd energy. Podium or stage somewhere in background. Signage. The scale of movement influence is visible. | NPC in foreground crowd or at podium. Background is crowd density and signage. | Signage aesthetics are era-specific. Era_1: printed banners and signs. Era_3+: screens and digital displays appear. |
| `home_dining` | Domestic. The most personal meeting setting. Real food, real table, a home that reflects the occupant. The warmth here is earned, not designed. | NPC across table. Framing intimate. Background is domestic detail — bookshelves, photos, kitchen visible. | Appliances change by era. Era_1: no flatscreen TVs in background. Era_2+: flat screens appear on walls. |
| `home_office` | The player's space. Personal and unobserved. Stacks of things. A desk that is actually used. | Solo scene (player character implied). Framing from seated position. | Computer: CRT monitor mandatory era_1. Laptop becomes primary era_3+. |
| `hospital` | Clinical but not cold — the human weight of the setting is in the detail. Fluorescent overheads, vinyl floors, the particular quality of hospital light. | NPC in bed or beside bed. Framing is close and quiet. | Medical equipment changes by era — era_1 has bulkier monitors. |
| `phone_call` | No setting illustration — this card type uses a phone/device foreground with an atmospheric background that fades to black. The NPC is represented by their portrait in a circular crop, not a scene setting. | Phone or handset in foreground. NPC portrait inset. Background is an atmospheric blur of wherever the player is. | Phone form factor is era-critical. Era_1: bar or clamshell mobile, or corded landline. Era_2: early smartphone. Era_3+: modern smartphone. |
| `video_call` | Screen foreground showing the NPC in their environment. The NPC's background setting provides context about where they are. | Screen frame occupies most of the image. NPC visible within screen. Their environment visible behind them. | Screen quality and form factor change by era. Era_1: low-res video, desktop webcam. Era_2+: laptop camera. |
| `moving_vehicle` | Interior of a car, train, or plane. The world moving past the window. Privacy through motion. | NPC beside player in vehicle. Window shows motion-blurred exterior. | Vehicle interior: era_1 has no touchscreens, analog dials. Era_3+: infotainment screens appear. |

### 4.2 Presentation Tier Modulation

The `presentation_tier` (cold / neutral / warm) is the primary variable the Claude Prompt Builder uses to adjust the palette, lighting, and composition of a background.

These instructions are injected into every image prompt:

**Cold (npc_presentation_state 0.0–0.33):**
> Apply cool, desaturated palette to the setting. Hard directional light with deep shadows. Tight framing — the camera is closer to the subject, reducing space. The setting should feel like a constraint, not an environment. Shadow occupies more of the frame than light.

**Neutral (npc_presentation_state 0.34–0.66):**
> Apply the setting's natural palette without strong warming or cooling. Moderate contrast. Standard composition for the setting. The space is present but not expressive.

**Warm (npc_presentation_state 0.67–1.0):**
> Apply warm-shifted palette — golden light, lower contrast, softer shadows. Wider framing — more space around the subject. The setting feels comfortable. Light occupies more of the frame than shadow.

### 4.3 Era Visual Reference Injection

The following era notes are injected into every background image prompt's era context:

| Era | Key visual markers |
|---|---|
| `era_1` (2000–2007) | No smartphones. CRT monitors or early flat-panel LCDs. Clamshell or bar-form mobile phones. Late 1990s–early 2000s vehicle styles. Dial-up modem equipment visible in home/office settings. Pre-HD television (4:3 ratio). CCTV cameras rare outside high-security settings. |
| `era_2` (2007–2013) | Early smartphones (iPhone/Android) beginning to appear, not universal. Flat-screen monitors now standard. Hybrid vehicles beginning to appear. HD television. Social media-era street advertising starts to appear (QR codes, URLs prominent on signage). |
| `era_3` (2013–2019) | Smartphones universal. Tablets on desks. Touchscreen kiosks. Electric vehicles occasionally visible. Open-plan offices more common. Streaming-era aesthetics in home environments (no physical media visible). |
| `era_4` (2019–2024) | COVID-era residuals: masks occasionally present, plexiglass dividers in public settings. Remote work aesthetics (home office setups more elaborate). EV vehicles more common. Delivery robots/services beginning to appear in urban backgrounds. |
| `era_5` (2024–2035) | Mature EV landscape. AI assistant displays in offices. Autonomous vehicle forms visible in traffic. Changed urban infrastructure from energy transition. More visible inequality — both high-tech luxury and visible poverty in the same frame. |

---

## 5. Music Generation

### 5.1 Track Taxonomy

Music tracks are pre-generated per the following three-dimension matrix. Each combination receives one looping track of ~3 minutes minimum.

**Dimension 1 — Era (5 values, V1):**
Each era has a distinct musical palette drawing from real-world music of that period, filtered through the game's non-intrusive atmospheric requirement.

| Era | Palette description | Instrumentation anchors |
|---|---|---|
| `era_1` | Late 1990s–early 2000s. Downtempo, trip-hop, post-bop jazz, early electronica. Y2K aesthetic of optimism with underlying unease. | Bass, brushed drums, Rhodes piano, sparse synth pads, upright bass |
| `era_2` | 2007–2013. Fragmentation and disruption. Minimal techno, post-rock, contemporary classical. The financial crisis is present as tension in the music. | Piano, string quartet, glitch electronics, sparse kick |
| `era_3` | 2013–2019. Acceleration. Confident complexity. Neo-soul, ambient R&B, contemporary jazz, Afrobeat-influenced. | Live drums, layered keys, bass guitar, brass |
| `era_4` | 2019–2024. Fracture. Uncertain, compressed, urgent. Ambient electronic, dark jazz, contemporary classical with dissonance. | Piano, electronics, cello, fractured rhythm |
| `era_5` | 2024–2035. Transition. Emergent, open-ended, unresolved. Ambient, generative-style, post-classical. | Modular synthesis, piano, processed strings |

**Dimension 2 — Tension tier (4 values):**

| Tension | Description | Musical character |
|---|---|---|
| `neutral` | No active challenge; maintenance and drift | Ambient, minimal, unhurried. Can almost disappear into the background. |
| `survival` | Scarcity and pressure; decisions have immediate material stakes | Sparse, anxious undercurrent. Rhythm present but not aggressive. |
| `dominance` | Strategic competition; resources deployed, rivals reacting | Confident, propulsive, but not triumphant. Forward momentum. |
| `preservation` | Power under threat; sustained opposition from multiple directions | Complex, layered, unresolved. Tension without resolution. Occasional near-silence. |

**Dimension 3 — Setting category (5 values):**
These are broader than the individual settings — each setting falls into one category.

| Category | Settings included | Musical character |
|---|---|---|
| `business_interior` | boardroom, private_office, open_plan_office, laboratory | Composed, professional. Minimal texture — the music should not distract from decision-making. |
| `industrial` | factory_floor, warehouse, construction_site | Lower register. Industrial texture without being aggressive. |
| `social` | restaurant, cafe, hotel_lobby, nightclub, public_park, political_rally | Higher warmth. More presence. The social texture is in the music. |
| `institutional` | government_office, courthouse, courtroom, police_station, prison_visiting, prison_cell | Formal restraint. The institution has its own gravity. No warmth. |
| `personal` | home_dining, home_office, hospital, moving_vehicle, street_corner, parking_garage, phone_call, video_call | Variable. This category contains the widest emotional range. Music follows the tension tier more than the setting category. |

**Total music tracks:** 5 eras × 4 tension tiers × 5 setting categories = **100 tracks**.

### 5.2 Music Prompt Template

Claude constructs music prompts using the following template structure. The output is a prose description suitable for music generation APIs:

```
[Era palette description]. [Tension character]. [Setting category character].
Instrumentation: [2–4 instruments from era anchors, adjusted for tension].
Tempo: [BPM range appropriate to tension: survival=65–80, dominance=85–100, preservation=55–70, neutral=55–75].
No lyrics. No recognizable melody. Loopable. Minimum 3 minutes.
Avoid: dramatic swells, cinematic orchestration, anything that calls attention to itself.
```

---

## 6. Sound Generation

### 6.1 Ambient Sound Loops

One ambient loop per SceneSetting. The ambient loop plays under music (at lower volume) and provides the physical texture of being in that space. Duration: 60–90 seconds, seamlessly loopable.

Claude constructs ambient prompts by describing the actual physical sounds of the environment. The prompt should read like a sound designer's field notes, not a creative brief.

**Prompt template:**
> "60-second looping ambient sound for [setting]. Physical sounds: [list 3–5 distinct sound sources present in this space, with their character]. Mix: [foreground vs. background balance, density]. No music. No dialogue. No single dominant sound — this is environmental texture."

**Setting-specific notes for Claude:**

| Setting | Dominant sound sources |
|---|---|
| `boardroom` | HVAC hum (low, steady), occasional chair movement, distant city through glass, pen on paper |
| `private_office` | Computer fan (era_1: louder), HVAC, occasional phone ring distant |
| `open_plan_office` | Keyboard clicks (density: medium), phone conversations (low intelligibility), movement, printer |
| `factory_floor` | Machinery rhythm (industrial, not musical), conveyor hum, forklift beeps, worker voices (low intelligibility) |
| `warehouse` | Echo. Distant forklift. Settling metal shelves. Wind from loading dock. Very sparse. |
| `construction_site` | Intermittent power tools, concrete mixer, scaffolding metal, outdoor wind, shouts |
| `laboratory` | Clean room hum, ventilation, instrument beeps (sparse), glassware sounds |
| `restaurant` | Ambient conversation (unintelligible), cutlery and plates, kitchen sounds distant, soft music suggestion |
| `cafe` | Espresso machine (periodic), conversation murmur, door opening, street sound from entrance |
| `hotel_lobby` | Marble echo, distant conversation, elevator ding, rolling luggage |
| `nightclub` | Heavy bass thump (muffled, as if from the main floor), crowd movement, glass sounds |
| `government_office` | Fluorescent hum, paper shuffling, phone rings, filing cabinet, HVAC |
| `courthouse` | Stone echo, footsteps reverb, low conversation, door closing heavy |
| `courtroom` | Near-silence in session. HVAC. Paper. Occasional cough. Gravity in the quiet. |
| `police_station` | Radio chatter (low intelligibility), distant desk sounds, door buzz, fluorescent hum |
| `prison_visiting` | Glass or mesh acoustic effect on voices (not words, just texture), HVAC, distant institutional sounds |
| `prison_cell` | Extreme quiet with institutional sounds: distant doors, footsteps on concrete, HVAC |
| `street_corner` | Traffic (era-appropriate), pedestrian footsteps, distant voices, wind, birds |
| `public_park` | Birds, wind through leaves, distant children, jogger footsteps, water if near fountain |
| `parking_garage` | Echo. Distant vehicle. Fluorescent flicker sound. Footsteps reverb. Very sparse. |
| `political_rally` | Crowd murmur building, PA system hum, chanting at distance, applause waves |
| `home_dining` | Cutlery, quiet conversation texture, kitchen sounds, family ambient |
| `home_office` | Computer fan, street ambient through window, occasional creak of chair |
| `hospital` | Soft PA announcements (unintelligible), cart wheels on vinyl, HVAC, distant beeping |
| `phone_call` | Line presence (slight hiss, room tone of the other end), no music |
| `video_call` | Compressed audio texture, fan noise from computer, soft keyboard |
| `moving_vehicle` | Road sound, engine hum (era-accurate: era_1 petrol engine, era_5 near-silent EV), wind |

### 6.2 UI Event Sounds

These sounds accompany UI state changes and card arrivals. They should be brief, unobtrusive, and tonally consistent with the game's aesthetic — no sharp electronic bleeps, no cinematic stabs.

| Event | Description for Claude | Duration |
|---|---|---|
| `mandatory_card_arrive` | Attention required. Not alarming — important. A low, resonant tone that resolves downward. | 600–900ms |
| `timed_optional_card_arrive` | A soft, inviting sound. The world has something to offer or ask. Upward inflection, small. | 400–600ms |
| `ambient_card_arrive` | Nearly silent. A very soft notification texture — barely there. | 200–300ms |
| `card_dismiss` | Paper or cloth texture. The card resolves. Brief, complete. | 200–400ms |
| `obligation_node_escalated` | A note that should feel like a subtle shift — not alarming but not neutral. Something has changed. | 500–700ms |
| `obligation_node_critical` | The same tone as escalated but lower, heavier. The weight has increased. | 600–800ms |
| `calendar_entry_imminent` | Soft clock-adjacent sound. Not ticking — a single resonant beat. | 300–500ms |
| `skill_gain` | A small, warm resolution. Growth confirmed. | 400–600ms |
| `fast_forward_start` | A brief whoosh texture — time moving. | 200–400ms |
| `fast_forward_stop` | The reverse — settling into present moment. | 200–400ms |

### 6.3 Narrative Event Sounds

These accompany specific simulation events that don't have scene cards but do have audio significance.

| Event | Description | Duration |
|---|---|---|
| `phone_ring_incoming` | Era-appropriate phone ring. Era_1: mobile polyphonic ringtone or desk phone bell. Era_3+: modern smartphone ring. | 2–4 seconds |
| `door_knock` | A physical knock on a door — three times. Deliberate, not aggressive. | 1–2 seconds |
| `vehicle_arrival` | Car pulling up and stopping. Engine off. Era-appropriate vehicle sound. | 3–5 seconds |
| `paper_shuffle` | Documents being handled. Physical paper. | 1–2 seconds |
| `handshake_cloth` | The sound of a formal handshake — cloth, presence. | 300–500ms |
| `crowd_reaction_positive` | Crowd applause or approval. For political events. | 3–5 seconds |
| `crowd_reaction_negative` | Crowd murmur, boos, unrest. | 3–5 seconds |
| `cash_physical` | Physical banknotes being counted or handed over. | 1–2 seconds |

---

## 7. NPC Portrait Profile (TDD Gap)

The NPC struct in TDD v29 §4 does not include visual appearance or flavour fields. The following struct is added. All fields are populated by the world-gen pipeline — none are set during gameplay.

```cpp
struct NPCPortraitProfile {
    uint32_t npc_id;

    // Both fields generated by the NPC Randomizer (§2.4). LOCKED after world-gen.
    std::string character_description;  // Appearance description. Used verbatim in all
                                        // portrait image prompts for this NPC.
                                        // Never displayed to the player.
    std::string backstory_blurb;        // 2–3 sentence flavour text. Displayed in the
                                        // NPC profile panel when the player inspects them.
                                        // Not mechanically used by the simulation.

    // Set true after all three portrait variants pass quality review.
    bool portraits_generated;
};
```

**Portrait variants per NPC (all generated at world-gen time):**

| Variant | File | When used |
|---|---|---|
| `cold` | `npc_{id}_portrait_cold.{ext}` | npc_presentation_state 0.0–0.33 |
| `neutral` | `npc_{id}_portrait_neutral.{ext}` | 0.34–0.66 |
| `warm` | `npc_{id}_portrait_warm.{ext}` | 0.67–1.0 |

All three variants use `character_description` verbatim — only palette, body language, and expression change between tiers.

**Promoted NPCs (background → Significant during gameplay):** A promoted NPC's `NPCPortraitProfile` will be empty. V1 uses a role-appropriate placeholder from a pre-generated pool (one portrait per NPC role, generated as part of the pre-gen pipeline). The placeholder does not use the NPC's specific `character_description`. Per-NPC portrait generation for promoted NPCs is a post-launch pipeline improvement.

**No aging variants in V1.** Portraits do not update as NPCs age in-game. Deferred post-launch.

---

## 8. Asset Naming Convention and Library Structure

```
/assets/
  /scene_backgrounds/
    bg_{setting}_{cold|neutral|warm}_{era_1..5}_{day|night}.{ext}
    Example: bg_parking_garage_cold_era_1_night.png

  /npc_portraits/
    npc_{id}_portrait_{cold|neutral|warm}.{ext}
    Example: npc_4821_portrait_neutral.png

  /npc_portraits/placeholders/
    placeholder_{role}_{cold|neutral|warm}.{ext}
    Example: placeholder_regulator_neutral.png

  /music/
    music_{era_1..5}_{survival|dominance|preservation|neutral}_{business_interior|industrial|social|institutional|personal}.{ext}
    Example: music_era_2_preservation_institutional.mp3

  /ambient/
    ambient_{setting}.{ext}
    Example: ambient_parking_garage.mp3

  /sfx/
    sfx_{event_type}.{ext}
    Example: sfx_mandatory_card_arrive.mp3
```

**Asset lookup at scene card display time:**

| Asset | Key construction |
|---|---|
| Background | `setting` + `npc_presentation_state → {cold\|neutral\|warm}` + `current_tick → SimulationEra` + `time_of_day → {day\|night}` |
| NPC portrait | `npc_id` + `npc_presentation_state → {cold\|neutral\|warm}` |
| NPC portrait (promoted, no specific portrait) | `npc_role` + `npc_presentation_state → {cold\|neutral\|warm}` → placeholder pool |
| Music | `SimulationEra` + `tension_tier` + `setting → setting_category` |
| Ambient | `setting` |

**Fallback chain:** If exact asset is missing → neutral presentation tier → if still missing → log error and skip (no silent fallback that masks pipeline gaps). Era fallback for backgrounds: era_1 is the last resort. The pipeline should never produce a gap; fallbacks exist to protect against incomplete pipeline runs, not as a design feature.

---

## 9. Quality Checklist

Claude's `style_anchor_check` field in the output JSON must explicitly confirm each of the following. If any check fails, Claude should revise the prompt before outputting it.

**For image prompts:**

- [ ] Style includes: oil painting, gouache, visible brushwork, painterly, atmospheric
- [ ] Negative prompt includes: photorealistic, hyperrealistic, 3D render, CGI, digital painting (clean), anime, cartoon, flat illustration, text, watermark
- [ ] Lower third of composition is atmospherically dark (dialogue panel area)
- [ ] NPC positioned left or right foreground — not centered
- [ ] Maximum three figures in scene
- [ ] Era-accurate technology visible in background
- [ ] Presentation tier palette direction correctly applied (cool/desaturated vs. warm/saturated)
- [ ] NPC character description included verbatim (portrait prompts only)

**For music prompts:**

- [ ] No lyrics specified or implied
- [ ] Era palette instruments referenced
- [ ] Tension tier character correctly applied
- [ ] Explicit instruction to avoid cinematic orchestration
- [ ] Loop-appropriate duration specified

**For sound prompts:**

- [ ] Physical sound sources listed (not abstract descriptions)
- [ ] No music specified in ambient prompts
- [ ] Duration range specified
- [ ] Era-appropriate for technology-dependent sounds (phone rings, vehicle sounds)

---

*EconLife AI Generation Spec v0.1 — Companion to GDD v1.7 / TDD v29 / Scene Card Rulebook v0.1 — Living Document*
