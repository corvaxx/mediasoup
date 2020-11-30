"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Mixer = void 0;
const EnhancedEventEmitter_1 = require("./EnhancedEventEmitter");
const Logger_1 = require("./Logger");
const Producer_1 = require("./Producer");
const uuid_1 = require("uuid");
const logger = new Logger_1.Logger('Mixer');
class Mixer extends EnhancedEventEmitter_1.EnhancedEventEmitter {
    constructor({ internal, channel, payloadChannel, appData }) {
        super();
        // Close flag.
        this._closed = false;
        // Producers map.
        this._producers = new Map();
        logger.debug('constructor()');
        this._internal = internal;
        this._channel = channel;
        this._payloadChannel = payloadChannel;
        this._appData = appData;
    }
    /**
     * Mixer id.
     */
    get id() {
        return this._internal.mixerId;
    }
    /**
     * Whether the Mixer is closed.
     */
    get closed() {
        return this._closed;
    }
    /**
     * Close the Mixer.
     */
    close() {
        if (this._closed) {
            return;
        }
        logger.debug('close()');
        this._closed = true;
        // Remove notification subscriptions.
        // this._channel.removeAllListeners(this._internal.transportId);
        // this._payloadChannel.removeAllListeners(this._internal.transportId);
        this._channel.request('mixer.close', this._internal)
            .catch(() => { });
        // Close every Producer.
        for (const producer of this._producers.values()) {
            producer.transportClosed();
            // Must tell the Router.
            this.emit('@producerclose', producer);
        }
        this._producers.clear();
        // Close every Consumer.
        // for (const consumer of this._consumers.values())
        // {
        //     consumer.transportClosed();
        // }
        // this._consumers.clear();
        this.emit('@close');
    }
    async produce(kind) {
        logger.debug('produce()');
        if (!['video'].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);
        const internal = { ...this._internal, producerId: uuid_1.v4() };
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
        this._producers.set(producer.id, producer);
        producer.on('@close', () => {
        });
        this.emit('@newproducer', producer);
        return producer;
    }
}
exports.Mixer = Mixer;
