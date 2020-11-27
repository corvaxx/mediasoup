
import { Channel } from './Channel';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { Logger } from './Logger';
import { MediaKind } from './RtpParameters';
import { PayloadChannel } from './PayloadChannel';
import { Producer, ProducerOptions } from './Producer';
import { v4 as uuidv4 } from 'uuid';


const logger = new Logger('Mixer');

export class Mixer extends EnhancedEventEmitter
{
    // Internal data.
    protected readonly _internal:
    {
        routerId: string;
        mixerId: string;
    };

    // Channel instance.
    protected readonly _channel: Channel;

    // PayloadChannel instance.
    protected readonly _payloadChannel: PayloadChannel;

    // Custom app data.
    private readonly _appData?: any;

    // Close flag.
    protected _closed = false;

    // Producers map.
    protected readonly _producers: Map<string, Producer> = new Map();

    constructor({ internal, channel, payloadChannel, appData } : 
    { 
        internal       : any;
        channel        : Channel; 
        payloadChannel : PayloadChannel;
        appData        : any;
    })
    {
        super();

        logger.debug('constructor()');

        this._internal       = internal;
        this._channel        = channel;
        this._payloadChannel = payloadChannel;
        this._appData        = appData;
    }

    /**
     * Mixer id.
     */
    get id(): string
    {
        return this._internal.mixerId;
    }

    /**
     * Whether the Mixer is closed.
     */
    get closed(): boolean
    {
        return this._closed;
    }

    /**
     * Close the Transport.
     */
    close(): void
    {
        if (this._closed)
        {
            return;
        }

        logger.debug('close()');

        this._closed = true;

        // Remove notification subscriptions.
        // this._channel.removeAllListeners(this._internal.transportId);
        // this._payloadChannel.removeAllListeners(this._internal.transportId);

        this._channel.request('mixer.close', this._internal)
            .catch(() => {});

        // Close every Producer.
        for (const producer of this._producers.values())
        {
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

    async produce(kind:MediaKind)
        : Promise<Producer>
    {
        logger.debug('produce()');

        if (![ 'video' ].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);

        const internal = { ...this._internal, producerId: uuidv4() };
        const reqData  = { kind };

        const status =
            await this._channel.request('mixer.produce', internal, reqData);

        const data    = {};
        const appData = {};


        const producer = new Producer(
            {
                internal,
                data,
                payloadChannel : this._payloadChannel,
                channel        : this._channel,
                appData,
                paused         : false
            });

        this._producers.set(producer.id, producer);

        producer.on('@close', () =>
        {
        });

        this.emit('@newproducer', producer);

        return producer;
    }
}