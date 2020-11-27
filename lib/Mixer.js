"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Mixer = void 0;
const EnhancedEventEmitter_1 = require("./EnhancedEventEmitter");
const Logger_1 = require("./Logger");
const Producer_1 = require("./Producer");
const logger = new Logger_1.Logger('Mixer');
class Mixer extends EnhancedEventEmitter_1.EnhancedEventEmitter {
    constructor({ channel, payloadChannel }) {
        super();
        logger.debug('constructor()');
        this._channel = channel;
        this._payloadChannel = payloadChannel;
    }
    async produce(kind) {
        logger.debug('produce()');
        if (!['video'].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);
        const internal = {};
        const reqData = { kind };
        const status = await this._channel.request('mixer.produce', internal, reqData);
        const data = {};
        const appData = {};
        const producer = new Producer_1.Producer({
            internal,
            data,
            payloadChannel: this._payloadChannel,
            channel: this._channel,
            appData,
            paused: false
        });
        producer.on('@close', () => {
        });
        return producer;
    }
}
exports.Mixer = Mixer;
