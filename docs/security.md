# Security Model

> Packet encryption, replay resistance, sender validation, and
> failure-case analysis.
>
> This document describes the protections implemented in the current
> firmware. It does not claim the system is resistant to all attacks.
> Known limitations are listed in Section 8.

---

## Table of Contents

1. [Defence Layers](#1-defence-layers)
2. [AES-128 ECB Packet Encryption](#2-aes-128-ecb-packet-encryption)
3. [Rolling Counter (Replay Resistance)](#3-rolling-counter-replay-resistance)
4. [Paired-Sender Validation](#4-paired-sender-validation)
5. [Duplicate Suppression](#5-duplicate-suppression)
6. [Failure Cases](#6-failure-cases)
7. [Production Checklist](#7-production-checklist)
8. [Known Limitations](#8-known-limitations)

---

## 1. Defence Layers

<!-- DIAGRAM-TODO:
File: docs/flowcharts/security_layers.png
Purpose: Stacked diagram of four defence layers
Source format: draw.io
-->

[View security-layers diagram](flowcharts/security_layers.png)

ASCII fallback:
