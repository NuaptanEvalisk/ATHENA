-- Sort a namespace whose first capture is a Roman numeral.
-- Example template: %R
return {
    version = 1,
    key = function(fields)
        return { fields[1].roman }
    end,
}
