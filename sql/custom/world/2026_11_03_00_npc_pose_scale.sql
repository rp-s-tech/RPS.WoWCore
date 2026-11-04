-- Preserve legacy pose size once, before pose restoration delegates scale to creature.size.
-- An explicit native spawn scale takes precedence over the legacy pose.
UPDATE creature c
JOIN saved_npc p ON p.guid = c.guid AND p.entry = c.id
SET c.size = p.size
WHERE c.size <= 0 AND p.size > 0 AND p.size <> 1;
