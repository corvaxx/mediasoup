"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Mixer = void 0;
const EnhancedEventEmitter_1 = require("./EnhancedEventEmitter");
const Logger_1 = require("./Logger");
const Producer_1 = require("./Producer");
const uuid_1 = require("uuid");
const ortc = require("./ortc");
const logger = new Logger_1.Logger('Mixer');
class Mixer extends EnhancedEventEmitter_1.EnhancedEventEmitter {
    constructor({ internal, channel, payloadChannel, appData, getRouterRtpCapabilities }) {
        super();
        // Close flag.
        this._closed = false;
        // Producers map.
        this._producers = new Map();
        // rtp parameters
        this._rtpParameters = {
            "codecs": [
                {
                    "clockRate": 90000,
                    "mimeType": "video/H264",
                    "parameters": {
                        "level-asymmetry-allowed": 1,
                        "packetization-mode": 1,
                        "profile-level-id": "42e01f"
                    },
                    "payloadType": 125,
                    "rtcpFeedback": [
                        {
                            "parameter": "",
                            "type": "goog-remb"
                        },
                        {
                            "parameter": "",
                            "type": "transport-cc"
                        },
                        {
                            "parameter": "fir",
                            "type": "ccm"
                        },
                        {
                            "parameter": "",
                            "type": "nack"
                        },
                        {
                            "parameter": "pli",
                            "type": "nack"
                        }
                    ]
                },
                {
                    "clockRate": 90000,
                    "mimeType": "video/rtx",
                    "parameters": {
                        "apt": 125
                    },
                    "payloadType": 107,
                    "rtcpFeedback": []
                }
            ],
            "encodings": [
                {
                    "active": true,
                    "dtx": false,
                    "maxBitrate": 500000,
                    "rid": "r0",
                    "scalabilityMode": "S1T3",
                    "scaleResolutionDownBy": 4
                },
                {
                    "active": true,
                    "dtx": false,
                    "maxBitrate": 1000000,
                    "rid": "r1",
                    "scalabilityMode": "S1T3",
                    "scaleResolutionDownBy": 2
                },
                {
                    "active": true,
                    "dtx": false,
                    "maxBitrate": 5000000,
                    "rid": "r2",
                    "scalabilityMode": "S1T3",
                    "scaleResolutionDownBy": 1
                }
            ],
            "headerExtensions": [
                {
                    "encrypt": false,
                    "id": 4,
                    "parameters": {},
                    "uri": "urn:ietf:params:rtp-hdrext:sdes:mid"
                },
                {
                    "encrypt": false,
                    "id": 5,
                    "parameters": {},
                    "uri": "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
                },
                {
                    "encrypt": false,
                    "id": 6,
                    "parameters": {},
                    "uri": "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"
                },
                {
                    "encrypt": false,
                    "id": 2,
                    "parameters": {},
                    "uri": "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"
                },
                {
                    "encrypt": false,
                    "id": 3,
                    "parameters": {},
                    "uri": "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
                },
                {
                    "encrypt": false,
                    "id": 13,
                    "parameters": {},
                    "uri": "urn:3gpp:video-orientation"
                },
                {
                    "encrypt": false,
                    "id": 14,
                    "parameters": {},
                    "uri": "urn:ietf:params:rtp-hdrext:toffset"
                }
            ],
            "mid": "0",
            "rtcp": {
                "cname": "de1c8077",
                "reducedSize": true
            }
        };
        logger.debug('constructor()');
        this._internal = internal;
        this._channel = channel;
        this._payloadChannel = payloadChannel;
        this._appData = appData;
        this._getRouterRtpCapabilities = getRouterRtpCapabilities;
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
        if (this._producers.size === 0)
            throw new TypeError('no mixer producers');
        this._closed = true;
        // Remove notification subscriptions.
        // this._channel.removeAllListeners(this._internal.transportId);
        // this._payloadChannel.removeAllListeners(this._internal.transportId);
        var mixerProducer = this._producers.values().next().value;
        const internal = { ...this._internal, mixerProducerId: mixerProducer.id };
        this._channel.request('mixer.close', internal)
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
        if (this._producers.size !== 0)
            throw new TypeError('no more than one producer allowed');
        if (!['video'].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);
        var rtpParameters = this._rtpParameters;
        // This may throw.
        ortc.validateRtpParameters(rtpParameters);
        const routerRtpCapabilities = this._getRouterRtpCapabilities();
        // This may throw.
        const rtpMapping = ortc.getProducerRtpParametersMapping(rtpParameters, routerRtpCapabilities);
        // This may throw.
        const consumableRtpParameters = ortc.getConsumableRtpParameters(kind, rtpParameters, routerRtpCapabilities, rtpMapping);
        const internal = { ...this._internal, producerId: uuid_1.v4() };
        const reqData = { kind, rtpParameters, rtpMapping };
        const status = await this._channel.request('mixer.produce', internal, reqData);
        const data = {
            kind,
            rtpParameters,
            type: status.type,
            consumableRtpParameters
        };
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
        // producer.on('@close', () =>
        // {
        //     this._producers.delete(producer.id);
        //     this.emit('@producerclose', producer);
        // });
        this.emit('@newproducer', producer);
        return producer;
    }
    async add(producer, kind, options) {
        logger.debug('add()');
        if (this._producers.size === 0)
            throw new TypeError('no mixer producers');
        if (this._producers.has(producer.id))
            throw new TypeError(`a Producer with same id "${producer.id}" in mixer producers`);
        if (!['video'].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);
        var mixerProducer = this._producers.values().next().value;
        const internal = { ...this._internal, mixerProducerId: mixerProducer.id, producerId: producer.id };
        const reqData = { kind, options };
        const status = await this._channel.request('mixer.add', internal, reqData);
    }
}
exports.Mixer = Mixer;
