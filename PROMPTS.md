# PROMPTS.md

Every prompt given to Claude Code in the sessions that produced this project, verbatim and in
order (2026-09-12). Answers picked from multiple-choice question dialogs are marked *(choice)*;
attached photos are marked *[image]*; messages sent while Claude was still working are marked
*(mid-task)*.

## 1. Detecting the first board (Elecrow CrowPanel 5.0", later found to have a dead panel)

1. `I just connected some ind of ESP32 device to my computer via USB, can you detect it?`
2. `yes, read the flash size and set up the project`
3. *(choice)* Framework: **PlatformIO + Arduino** · Board: **Elecrow CrowPanel**
4. *(choice)* Size: **5.0" (800x480, RGB)**
5. `I think I need you to reset this device to factory defaults - I have a black screen and it seems unresponsive.`
6. *(choice)* PCB revision: **Can't tell — try V3 first**
7. `nothing displays`
8. `the panel is dark`
9. `ok, I plugged it in elsewhere but still nothing`
10. `It worked before, put the original firmware back`
11. `update platformio.ini to auto-detect the port`
12. `verify it still builds`

## 2. Identifying the second board (Elecrow CrowPanel 2.1" rotary, sold as "IoTeikXgo")

13. `no, forget all this, the unit display is dead.  I've attached another device, let me know if you detect it`
14. `This is where I bought it, help me figure out exactly what it is: https://www.amazon.com/dp/B0GQS8L5LT?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_9&th=1`
15. `Cool, save this infor in a markdown file for later reference.`

## 3. Designing the AI usage dial (now Pucky)

16. `Now, what I really want to do with this is to create an app that displays infomation for: 1) Claude Code, 2) Codex, 3) OpenRouter, 4) Gemini, 5) z.AI (GLM) and 6) Deepseek.  Basically, I want to display usage, credits left, etc.  It's a circular screen with a dial, so I want to turn the dial to the function I want and display the data for that particular AI provider.  Let's have a discussion about this, don't assume anything so ask clarifying questions.`
17. `1) Claude Code: Daily Usage/5-hour Usage, Weekly Usage, Reset date / time | Codex: Same as Claude Code | Gemini: Same as Claude Code | OpenRouter: Available Credits | Deepseek: Balance, Last 30 days (Cost/API Requests/Tokens) | z.AI: zcode session data (see attached image)  2) A 3) your discretion 4) rotate = switch provider, 5) autocycle 6)  every minute with warning thresholds as you suggest 7) your recommendation 8) yes, 9) Time, wifi status, total spend, lastr updated` *[image: ZCode "Usage Stats" page]*
18. `for each provider, I want their icon on the screen, so scrape them from the web`
19. `You know, all this data can be accessed via the web, so maybe we need to host everything on the ESP32 and forget using a local service / app`
20. `forget zcode, just focus on z.ai, you figure ou the rest, although I have claude, codex and gemini subscriptions`

## 4. Building it (host agent + firmware)

21. `ok, start building`
22. `I set the password`

## 5. 3D-printable dock

23. `now I need you to use the 3d skill to create a 3d printable enclosure for this.`
24. *(choice)* Part type: **Puck-style desk dock** (the other two questions were left to clarify)
25. `use the skill found here: D:\repos\3d-skills (install it)`
26. `1 - this is what it looks like[Image] 2 - yes that's mine, printing in PLA.  3 - ~15 degrees is good with a cable exit in back. 4 - drop in.` *[image: rear of the unit]*
27. `I need you to show me where I have to take the measurements`
28. `A - 47.7mm & 48.2mm | B - 12.0mm | C - ~12mm the cable sticks out the back | D - 80mm | E - the whole black body turns. | F - see images [Image] [Image] notice the three offsets for screws.` *[images: rear PCB with standoffs; side profile]*
29. `where is the image you sent?`
30. `S1 - each is 11.0mm | S2 - 4.4mm | S3 - 5.6mm | S5 - 6.0mm | S6 - [Image]` *[image: straight-down photo of the PCB with a ruler]*
31. `S4 - can't confirm but looks like M2 - screen orientation is fine`

## 6. This file

32. `Can you create a markdown file called PROMPTS.md that lists all the prompts I used to make this?`

## 7. Second session: finishing the providers (z.AI, OpenRouter, DeepSeek, Antigravity)

33. `I want you to add the other provider functionality - z.ai, openrouter, deepseek`
34. `doesn't deepseek also have tokens used and info like that?`
35. `my dashboard shows it so there must be a way - can you research this more? https://api-docs.deepseek.com/quick_start/token_usage` *[image: DeepSeek platform usage dashboard showing 30-day cost, API requests and tokens]*
36. `How do I change the time filter so the page refetches?`
37. `ok I added the session token, run it again`
38. `ok let's reflash`
39. *(mid-task)* `Let me know if it's easier to unplug the device and plug it back iun`
40. `I want to expand usage to include Antigravity (CLI agy) - this uses my Gemini account.  There is an SDK for agy, so research this and include this in the app as well.`
41. `add all the prompts used in this session to PROMPTS.md - look and see how we did it first.`
