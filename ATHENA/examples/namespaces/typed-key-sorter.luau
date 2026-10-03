-- Example for a template with word, exact integer, and string captures.
-- Example template: %w %N %s
-- Strings use ATHENA's documented bytewise ordering.  fields[2].integer is an
-- exact signed 64-bit Luau integer, so values beyond IEEE-754's exact range do
-- not lose precision.
return {
    version = 1,
    key = function(fields)
        return {
            fields[1].text,
            fields[2].integer,
            fields[3].text,
        }
    end,
}
