const subscribers = new Set();

export function subscribe(callback) {
    subscribers.add(callback);
    return () => {
        subscribers.delete(callback);
    };
}

export function notify(notification) {
    for (const callback of subscribers) {
        callback(notification);
    }
}
