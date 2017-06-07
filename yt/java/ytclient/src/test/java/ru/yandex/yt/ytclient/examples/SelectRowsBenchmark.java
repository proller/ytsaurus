package ru.yandex.yt.ytclient.examples;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import com.codahale.metrics.ConsoleReporter;
import com.codahale.metrics.Histogram;
import com.codahale.metrics.JmxReporter;
import com.codahale.metrics.Meter;
import com.codahale.metrics.MetricRegistry;
import com.codahale.metrics.SharedMetricRegistries;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import joptsimple.OptionSpec;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import ru.yandex.yt.ytclient.bus.BusConnector;
import ru.yandex.yt.ytclient.proxy.ApiServiceClient;
import ru.yandex.yt.ytclient.rpc.BalancingRpcClient;
import ru.yandex.yt.ytclient.rpc.RpcClient;
import ru.yandex.yt.ytclient.rpc.RpcOptions;
import ru.yandex.yt.ytclient.wire.UnversionedRowset;

/**
 * Created by aozeritsky on 26.05.2017.
 */
public class SelectRowsBenchmark {
    private static final Logger logger = LoggerFactory.getLogger(SelectRowsBenchmark.class);

    static int inflight = 0;
    static int maxInflight = 3000;
    static int rpsLimit = 30000;

    static final Lock lock = new ReentrantLock();
    static final Condition notFull = lock.newCondition();

    static class RequestGroup {
        final List<String> requests = new ArrayList<>();
    }

    // runme: --proxy n0035-myt.seneca-myt.yt.yandex.net,n0036-myt.seneca-myt.yt.yandex.net,n0037-myt.seneca-myt.yt.yandex.net --input requests
    public static void main(String[] args) throws Exception {
        final String user = ExamplesUtil.getUser();
        String token = ExamplesUtil.getToken();
        int niothreads = 4;
        int threads = 12;

        final MetricRegistry metrics = SharedMetricRegistries.getOrCreate("ytclient");
        final Histogram requestsHistogram = metrics.histogram(MetricRegistry.name(SelectRowsBenchmark.class, "requests", "histogram"));
        final Meter requestsMeter = metrics.meter(MetricRegistry.name(SelectRowsBenchmark.class,"requests", "meter"));
        final Histogram callHistogram = metrics.histogram(MetricRegistry.name(SelectRowsBenchmark.class,"calls", "histogram"));

        OptionParser parser = new OptionParser();

        OptionSpec<String> proxyOpt = parser.accepts("proxy", "proxy")
            .withRequiredArg().ofType(String.class).withValuesSeparatedBy(',');
        OptionSpec<String> tokenOpt = parser.accepts("token", "token")
            .withRequiredArg().ofType(String.class);
        OptionSpec<String> inputOpt = parser.accepts("input", "input")
            .withRequiredArg().ofType(String.class);
        OptionSpec<Integer> switchTimeoutOpt = parser.accepts("switchtimeout", "switchtimeout")
            .withRequiredArg().ofType(Integer.class);
        OptionSpec<Integer> threadsOpt = parser.accepts("threads", "threads")
            .withRequiredArg().ofType(Integer.class);
        OptionSpec<Integer> nioThreadsOpt = parser.accepts("niothreads", "niothreads")
            .withRequiredArg().ofType(Integer.class);
        OptionSpec<Integer> rpsLimitOpt = parser.accepts("rps", "rps")
            .withRequiredArg().ofType(Integer.class);
        OptionSpec<Integer> inflightOpt = parser.accepts("inflight", "inflight")
            .withRequiredArg().ofType(Integer.class);

        List<String> proxies = null;
        final ArrayList<RequestGroup> requests = new ArrayList<>();
        Duration localTimeout = Duration.ofMillis(60);
        Duration pingTimeout = Duration.ofMillis(1000);
        ExecutorService executorService;
        final LinkedBlockingQueue<RequestGroup> queue = new LinkedBlockingQueue<>(threads*2);

        OptionSet option = parser.parse(args);

        if (option.hasArgument(tokenOpt)) {
            token = option.valueOf(tokenOpt);
        }

        if (option.hasArgument(proxyOpt)) {
            proxies = option.valuesOf(proxyOpt);
        } else {
            parser.printHelpOn(System.out);
            System.exit(1);
        }

        if (option.hasArgument(rpsLimitOpt)) {
            rpsLimit = option.valueOf(rpsLimitOpt);
        }
        if (option.hasArgument(inflightOpt)) {
            maxInflight = option.valueOf(inflightOpt);
        }

        requests.add(new RequestGroup());

        if (option.hasArgument(inputOpt)) {
            Stream<String> lines = Files.lines(Paths.get(option.valueOf(inputOpt)));
            lines.forEach(line -> {
                String newLine = line.trim();
                if (newLine.isEmpty()) {
                    requests.add(new RequestGroup());
                } else {
                    requests.get(requests.size() - 1).requests.add(newLine);
                }
            });
        } else {
            parser.printHelpOn(System.out);
            System.exit(1);
        }

        if (option.hasArgument(switchTimeoutOpt)) {
            localTimeout = Duration.ofMillis(option.valueOf(switchTimeoutOpt));
        }
        if (option.hasArgument(threadsOpt)) {
            threads = option.valueOf(threadsOpt);
        }
        if (option.hasArgument(nioThreadsOpt)) {
            niothreads = option.valueOf(nioThreadsOpt);
        }

        final BusConnector connector = ExamplesUtil.createConnector(niothreads);
        final String finalToken = token;

        ConsoleReporter reporter = ConsoleReporter.forRegistry(metrics)
            .convertRatesTo(TimeUnit.SECONDS)
            .convertDurationsTo(TimeUnit.MILLISECONDS)
            .build();
        reporter.start(5, TimeUnit.SECONDS);

        final JmxReporter jmxReporter = JmxReporter.forRegistry(metrics).build();
        jmxReporter.start();

        executorService = Executors.newFixedThreadPool(threads);

        List<RpcClient> proxiesConnections = proxies.stream().map(x ->
            ExamplesUtil.createRpcClient(connector, user, finalToken, x, 9013)
        ).collect(Collectors.toList());

        RpcClient rpcClient = new BalancingRpcClient(
            localTimeout,
            pingTimeout,
            proxiesConnections.toArray(new RpcClient[proxiesConnections.size()])
        );

        final ApiServiceClient client = new ApiServiceClient(rpcClient,
            new RpcOptions().setDefaultTimeout(Duration.ofSeconds(5)));

        // TODO: wait connected
        TimeUnit.MILLISECONDS.sleep(10000);

        for (int i = 0; i < threads; ++i) {
            executorService.execute(() -> {
                for (;;) {
                    RequestGroup request;

                    try {
                        request = queue.take();
                    } catch (InterruptedException e) {
                        continue;
                    }

                    lock.lock();

                    try {
                        while (inflight >= maxInflight) {
                            notFull.await();
                        }

                        inflight ++;
                    } catch (InterruptedException e) {
                        continue;
                    } finally {
                        lock.unlock();
                    }

                    try {
                        long t0 = System.nanoTime();
                        List<CompletableFuture<UnversionedRowset>> futures = request
                            .requests.stream()
                            .map(s -> {
                                long t00 = System.nanoTime();
                                CompletableFuture<UnversionedRowset> r = client.selectRows(s);
                                long t11 = System.nanoTime();
                                callHistogram.update((t11 - t00) / 1000000);
                                return r;
                            }).collect(Collectors.toList());

                        CompletableFuture
                            .allOf(futures.toArray(new CompletableFuture[futures.size()]))
                            .whenComplete((a, b) -> {

                                long t1 = System.nanoTime();
                                requestsHistogram.update((t1 - t0) / 1000000);
                                requestsMeter.mark();

                                try {
                                    lock.lock();
                                    inflight --;
                                    if (inflight < maxInflight) {
                                        notFull.signalAll();
                                    }
                                } finally {
                                    lock.unlock();
                                }
                            });

                    } catch (Throwable e) {
                        logger.error("error", e);
                        // System.exit(1);
                    }
                }
            });
        }

        for (;;) {
            try {
                for (RequestGroup request : requests) {
                    queue.put(request);
                    long sleepTimeMs = 1000 / rpsLimit;
                    TimeUnit.MILLISECONDS.sleep(sleepTimeMs);
                }
            } catch (Throwable e) {
                logger.error("error `{}`", e.toString());
            }
        }
    }
}
