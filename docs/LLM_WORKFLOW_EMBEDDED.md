# LLM Workflow for Embedded Development

A practical guide for using LLMs effectively without over-reliance.

---

## Overview Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         EMBEDDED PROJECT WORKFLOW                            │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   PHASE 1    │    │   PHASE 2    │    │   PHASE 3    │    │   PHASE 4    │
│   Research   │───▶│    Design    │───▶│   Implement  │───▶│    Debug     │
│  (You + LLM) │    │    (You)     │    │  (You + LLM) │    │    (You)     │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
  ┌─────────┐        ┌─────────┐        ┌─────────┐        ┌─────────┐
  │Datasheet│        │Block    │        │Code     │        │Scope    │
  │Analysis │        │Diagram  │        │Writing  │        │Logic    │
  │         │        │Pin Map  │        │Register │        │Analyzer │
  │Existing │        │Memory   │        │Drivers  │        │Printf   │
  │Code     │        │Layout   │        │Tests    │        │Debug    │
  └─────────┘        └─────────┘        └─────────┘        └─────────┘

  LLM: 60%            LLM: 20%           LLM: 50%           LLM: 10%
  You: 40%            You: 80%           You: 50%           You: 90%
```

---

## Phase 1: Research & Understanding

### Your Tasks (Do First)
```
□ Read datasheet overview section
□ Identify key specifications
□ Understand the communication protocol
□ Note critical timing requirements
□ Sketch basic block diagram
```

### LLM Assistance (After You Understand Basics)

**Good Prompts:**
```
"Summarize the key registers needed to initialize the ADS1298 for basic operation"

"What are the critical timing requirements for ADS1298 SPI communication?"

"Compare SPI Mode 0 vs Mode 1 for this device based on the timing diagram"

"Extract the power-on sequence from this datasheet section: [paste relevant section]"
```

**Bad Prompts:**
```
"Write me a complete ADS1298 driver"  ← Too broad, you won't understand it

"How do I use ADS1298?"  ← Too vague, do basic research first

"Is this datasheet correct?"  ← LLM can't verify hardware specs
```

### Checklist Before Moving On
```
□ I can explain what the device does
□ I know the communication protocol
□ I understand the register map structure
□ I know the power and timing requirements
□ I have identified which features I need
```

---

## Phase 2: System Design

### Your Tasks (Mostly You)
```
□ Define hardware connections
□ Create pin mapping document
□ Design software architecture
□ Define module interfaces
□ Plan memory usage
□ Identify interrupt requirements
```

### LLM Assistance (Limited)

**Good Prompts:**
```
"What's the standard Zephyr driver architecture for SPI devices?"

"Review my pin mapping for potential conflicts: [paste your mapping]"

"What's a good folder structure for a Zephyr project with custom drivers?"
```

### Design Document Template
```
Project: ________________
MCU: ____________________
RTOS: ___________________

Hardware Connections:
┌─────────────┬─────────────┬─────────────┐
│ Peripheral  │ MCU Pin     │ Function    │
├─────────────┼─────────────┼─────────────┤
│             │             │             │
└─────────────┴─────────────┴─────────────┘

Software Modules:
┌─────────────────────────────────────────┐
│ Application Layer                       │
├─────────────────────────────────────────┤
│ Driver Layer                            │
├─────────────────────────────────────────┤
│ HAL / RTOS Layer                        │
├─────────────────────────────────────────┤
│ Hardware                                │
└─────────────────────────────────────────┘

Memory Budget:
- Flash: ______ / ______ KB
- RAM:   ______ / ______ KB
```

### Checklist Before Moving On
```
□ Hardware connections are documented
□ Software architecture is sketched
□ Module interfaces are defined
□ I know what each module will do
□ Memory budget is estimated
```

---

## Phase 3: Implementation

### Workflow Per Module

```
┌─────────────────────────────────────────────────────────────────┐
│  FOR EACH MODULE:                                               │
│                                                                 │
│  1. Write interface first (header file)         ← You          │
│  2. Implement basic structure                   ← You + LLM    │
│  3. Add register definitions                    ← LLM assist   │
│  4. Implement core functions                    ← You + LLM    │
│  5. Review every line                           ← You          │
│  6. Test on hardware                            ← You          │
│  7. Fix and iterate                             ← You          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### LLM-Assisted Implementation Strategy

#### Step 1: Define Interface Yourself
```c
/* You write this - defines YOUR requirements */
#ifndef ADS1298_H
#define ADS1298_H

int ads1298_init(void);
int ads1298_read_channel(uint8_t ch, int32_t *value);
int ads1298_set_gain(uint8_t ch, uint8_t gain);

#endif
```

#### Step 2: Ask LLM for Boilerplate
```
"Generate register definitions for ADS1298 based on this register map:
[paste register table from datasheet]"

"Write the SPI read/write functions for Zephyr following this interface:
[paste your header]"
```

#### Step 3: Verify Against Datasheet
```
For EVERY register definition:
□ Address matches datasheet
□ Bit positions are correct
□ Reset values are correct

For EVERY function:
□ Timing requirements met
□ Sequence matches datasheet
□ Error conditions handled
```

#### Step 4: Review Checklist
```
Before accepting ANY LLM code:

□ I understand what every line does
□ Register addresses verified against datasheet
□ Timing values verified against datasheet
□ Bit manipulations are correct
□ Error handling is appropriate
□ No hardcoded magic numbers without comments
□ Memory safety (buffer sizes, null checks)
```

### Good vs Bad LLM Usage

| Situation | Good Approach | Bad Approach |
|-----------|---------------|--------------|
| Register definitions | Ask LLM to format from datasheet table | Trust LLM's memory of register values |
| Init sequence | Ask LLM for structure, verify each step | Copy entire init without checking |
| Bit manipulation | Ask LLM to explain, then write yourself | Use LLM's bit shifts without verifying |
| Timing delays | Ask for formula, calculate yourself | Use LLM's delay values directly |
| Error handling | Ask for common patterns, adapt to your needs | Copy generic error handling |

---

## Phase 4: Debug & Test

### This Phase is Mostly YOU

```
LLM CANNOT:
├── See your oscilloscope
├── Measure actual timing
├── Know your PCB layout
├── Detect hardware failures
├── Know your specific setup
└── Verify real-world behavior
```

### Debug Workflow

```
┌─────────────────────────────────────────────────────────────────┐
│                      DEBUG FLOWCHART                            │
└─────────────────────────────────────────────────────────────────┘

            ┌─────────────┐
            │ Bug Found   │
            └──────┬──────┘
                   │
                   ▼
        ┌──────────────────────┐
        │ Is it hardware or    │
        │ software?            │
        └──────────┬───────────┘
                   │
       ┌───────────┴───────────┐
       │                       │
       ▼                       ▼
┌─────────────┐         ┌─────────────┐
│  HARDWARE   │         │  SOFTWARE   │
│  (You only) │         │ (You + LLM) │
└──────┬──────┘         └──────┬──────┘
       │                       │
       ▼                       ▼
┌─────────────┐         ┌─────────────────┐
│ • Scope     │         │ Can LLM help?   │
│ • Logic     │         │                 │
│   analyzer  │         │ • Syntax error  │
│ • Voltmeter │         │ • Logic error   │
│ • Check     │         │ • API usage     │
│   connections│        │ • Pattern issue │
└─────────────┘         └─────────────────┘
```

### When to Ask LLM for Debug Help

**Good:**
```
"My SPI read always returns 0xFF. Here's my init code and read function:
[paste code]
What common mistakes cause this?"

"This Zephyr API returns -ENODEV. What does this error mean and common causes?"

"Review this interrupt handler for race conditions:
[paste code]"
```

**Bad:**
```
"My code doesn't work, fix it"  ← Too vague

"Why isn't my hardware working?"  ← LLM can't diagnose hardware

"Debug this for me"  ← You need to isolate the problem first
```

### Hardware Debug Checklist (No LLM)
```
□ Power supply voltages correct?
□ All ground connections solid?
□ SPI signals visible on scope?
□ Clock frequency correct?
□ Signal integrity acceptable?
□ Correct SPI mode (CPOL/CPHA)?
□ Chip select timing correct?
□ Pull-up/pull-down resistors in place?
```

---

## Quick Reference: When to Use LLM

### High Value (Use LLM)
```
✓ Generating register definitions from datasheet tables
✓ Boilerplate code structure
✓ RTOS/framework API patterns
✓ Converting between data formats
✓ Documentation writing
✓ Explaining unfamiliar code
✓ Code review suggestions
✓ Test case generation
```

### Low Value (Do Yourself)
```
✗ Hardware debugging
✗ Timing-critical optimization
✗ Memory layout decisions
✗ Safety-critical code
✗ Real-time performance tuning
✗ PCB-related issues
✗ Verifying electrical specifications
✗ Final validation
```

---

## Prompt Templates for Embedded Development

### Datasheet Analysis
```
"From this datasheet section, extract:
1. Register address
2. Bit fields and their functions
3. Reset value
4. Any notes or warnings

[paste datasheet section]"
```

### Driver Generation
```
"Generate a [PROTOCOL] driver skeleton for [DEVICE] on [PLATFORM].
Requirements:
- Functions: init, read, write, [others]
- Use this coding style: [example]
- Handle these errors: [list]

I will verify all register values against the datasheet."
```

### Code Review
```
"Review this embedded code for:
1. Memory safety issues
2. Race conditions
3. Timing problems
4. Error handling gaps
5. Best practice violations

[paste code]"
```

### Debug Assistance
```
"I'm seeing [specific symptom] when [specific action].
Hardware: [MCU, peripherals]
Expected: [what should happen]
Actual: [what actually happens]
Already checked: [what you've verified]

[paste relevant code]

What are common causes for this?"
```

---

## Summary: The 5 Rules

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│  1. UNDERSTAND FIRST                                            │
│     Read datasheet before asking LLM                            │
│                                                                 │
│  2. VERIFY EVERYTHING                                           │
│     Never trust register values, timing, or specs from LLM      │
│                                                                 │
│  3. OWN THE ARCHITECTURE                                        │
│     Design decisions are yours, LLM assists implementation      │
│                                                                 │
│  4. HARDWARE DEBUG YOURSELF                                     │
│     LLM can't see your scope or logic analyzer                  │
│                                                                 │
│  5. IF YOU CAN'T EXPLAIN IT, DON'T USE IT                       │
│     Understand every line before it goes in your codebase       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Project-Specific Example: ADS1298

### What I (LLM) Helped With
```
✓ Register definitions structure
✓ Zephyr SPI driver pattern
✓ Device tree overlay syntax
✓ Workflow documentation
✓ Code organization
```

### What You Must Verify
```
□ All register addresses (compare to datasheet page XX)
□ SPI timing parameters
□ CONFIG register bit positions
□ Channel data format (24-bit two's complement)
□ DRDY timing requirements
□ Power-on reset duration
□ Internal reference settling time
```

### What You Must Do Yourself
```
□ Verify SPI signals with oscilloscope
□ Confirm DRDY signal behavior
□ Test actual sample rate
□ Validate ADC readings with known input
□ Check noise floor
□ Verify power supply stability
```
