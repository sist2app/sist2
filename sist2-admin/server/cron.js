const MINUTE = 60 * 1000;

function parseField(field, min, max) {
    const values = new Set();

    for (const part of field.split(",")) {
        let range = part;
        let step = 1;

        if (part.includes("/")) {
            const [rangePart, stepPart] = part.split("/");
            range = rangePart;
            step = Number(stepPart);
            if (!Number.isInteger(step) || step < 1) {
                throw new Error(`Invalid cron step: ${part}`);
            }
        }

        let start;
        let end;
        if (range === "*") {
            start = min;
            end = max;
        } else if (range.includes("-")) {
            const [startPart, endPart] = range.split("-");
            start = Number(startPart);
            end = Number(endPart);
        } else {
            start = Number(range);
            if (step === 1) {
                end = start;
            } else {
                end = max;
            }
        }

        if (!Number.isInteger(start) || !Number.isInteger(end) || start < min || end > max || start > end) {
            throw new Error(`Invalid cron field: ${field}`);
        }

        for (let value = start; value <= end; value += step) {
            values.add(value);
        }
    }

    return values;
}

export function cronMatches(expression, date) {
    const fields = expression.trim().split(/\s+/);
    if (fields.length !== 5) {
        throw new Error(`Invalid cron expression: ${expression}`);
    }

    const minutes = parseField(fields[0], 0, 59);
    const hours = parseField(fields[1], 0, 23);
    const daysOfMonth = parseField(fields[2], 1, 31);
    const months = parseField(fields[3], 1, 12);
    // 7 is a valid alias for Sunday (0)
    const daysOfWeek = parseField(fields[4], 0, 7);
    if (daysOfWeek.has(7)) {
        daysOfWeek.add(0);
    }

    if (!minutes.has(date.getMinutes())) {
        return false;
    }
    if (!hours.has(date.getHours())) {
        return false;
    }
    if (!months.has(date.getMonth() + 1)) {
        return false;
    }

    // Match pycron (used by the Python admin): every field must match,
    // including day-of-month AND day-of-week when both are restricted.
    return daysOfMonth.has(date.getDate()) && daysOfWeek.has(date.getDay());
}

export function startCron(onTick) {
    const scheduleNext = () => {
        const now = Date.now();
        const nextMinute = Math.ceil(now / MINUTE) * MINUTE;

        setTimeout(() => {
            onTick(new Date());
            scheduleNext();
        }, nextMinute - now + 50);
    };

    scheduleNext();
}
