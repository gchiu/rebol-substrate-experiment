# merchant_flow.jl — a tiny flow-based / dataflow demonstration in base Julia.
#
# A spreadsheet-like system with anonymous competing workers over a shared
# transaction pool, a content router, and per-concern reducers.  No packages,
# no threads, no distribution: Task + Channel only.
#
# The two edge kinds are deliberate:
#   * work sharing — the transaction pool is ONE channel with several competing
#     takers; each transaction is consumed exactly once.
#   * fan-out     — the router writes to THREE separate channels, one per
#     consumer, so each consumer receives only the consequences it cares about.
#
# Back-pressure note: there is a single router doing sequential put! calls onto
# the three bounded consumer channels.  If one consumer's channel fills, the
# router blocks on that put!, so the OTHER consumers also stop receiving for a
# while.  State is isolated per consumer, but scheduling is NOT: a slow consumer
# back-pressures the whole pipeline.  For v1 this is a feature, not a bug — it
# makes the flow visible.
#
# All messages are immutable; money is integer cents (never Float64), so the
# final totals are exact and independent of processing order.  Mutable state
# exists only inside the three reducer tasks and never leaves them: each reducer
# emits an immutable final result when its input channel closes.

# ---- message / value definitions --------------------------------------------

struct Transaction
    kind::Symbol       # :BUY | :SELL | :RETURN
    merchant::Symbol   # :merchant_a   (kept from day one for multi-merchant)
    item::Symbol       # :tea
    qty::Int
    price_cents::Int   # per-unit price in integer cents
end

struct Delta            # worker output: the atomic consequence of one transaction
    merchant::Symbol
    item::Symbol
    stock::Int          # signed units
    cash_cents::Int     # signed money
    sales::Int          # signed net-sales units
end

# router output: single-purpose typed consequences (fan-out / content routing)
struct StockConsequence; merchant::Symbol; item::Symbol; delta::Int; end
struct CashConsequence;  merchant::Symbol; delta_cents::Int;   end
struct SalesConsequence; merchant::Symbol; item::Symbol; delta::Int; end

# ---- pure business arithmetic (worker) --------------------------------------

function process(t::Transaction)::Delta
    p = t.price_cents
    if t.kind === :BUY
        Delta(t.merchant, t.item, +t.qty, -(t.qty * p), 0)
    elseif t.kind === :SELL
        Delta(t.merchant, t.item, -t.qty, +(t.qty * p), +t.qty)
    elseif t.kind === :RETURN
        Delta(t.merchant, t.item, +t.qty, -(t.qty * p), -t.qty)
    else
        error("unknown transaction kind: ", t.kind)
    end
end

# ---- display helper ---------------------------------------------------------

function dollars(cents::Int)
    sign = cents < 0 ? "-" : ""
    a = abs(cents)
    return string(sign, a ÷ 100, ".", lpad(string(a % 100), 2, '0'))
end

# ---- main -------------------------------------------------------------------

function main()
    transactions = [
        Transaction(:BUY,    :merchant_a, :tea, 10, 400),
        Transaction(:SELL,   :merchant_a, :tea,  3, 700),
        Transaction(:RETURN, :merchant_a, :tea,  1, 700),
        Transaction(:BUY,    :merchant_a, :tea,  5, 450),
        Transaction(:SELL,   :merchant_a, :tea,  4, 750),
        Transaction(:RETURN, :merchant_a, :tea,  2, 750),
    ]

    pool     = Channel{Transaction}(16)      # work queue (bounded)
    results  = Channel{Delta}(16)            # worker -> router
    stock_ch = Channel{StockConsequence}(8)  # router -> inventory
    cash_ch  = Channel{CashConsequence}(8)   # router -> cashflow
    sales_ch = Channel{SalesConsequence}(8)  # router -> sales

    # source: emit transactions, then close the pool (finally: even on error)
    source = @async begin
        try
            for t in transactions
                println("source: ", t.kind, " ", t.merchant, " ", t.item, " ",
                        t.qty, " @ ", dollars(t.price_cents))
                put!(pool, t)
            end
        finally
            close(pool)
        end
    end

    # workers: compete for the next transaction, map it to a Delta (pure)
    nworkers = 4
    workers = [@async begin
        for t in pool
            d = try
                process(t)
            catch e
                println("worker-", id, ": ERROR ", e, " (dropping ", t.kind, ")")
                continue
            end
            println("worker-", id, ": ", t.kind, " ", t.item, " ", t.qty,
                    " -> stock ", d.stock, "  cash ", dollars(d.cash_cents),
                    "  sales ", d.sales)
            put!(results, d)
        end
    end for id in 1:nworkers]

    # router: decompose each Delta, fan out typed consequences by content.
    # Sequential put! means a full downstream channel back-pressures the router.
    router = @async begin
        for d in results
            if d.stock != 0
                put!(stock_ch, StockConsequence(d.merchant, d.item, d.stock))
                println("router: stock ", d.stock, " -> inventory")
            end
            if d.cash_cents != 0
                put!(cash_ch, CashConsequence(d.merchant, d.cash_cents))
                println("router: cash ", dollars(d.cash_cents), " -> cashflow")
            end
            if d.sales != 0
                put!(sales_ch, SalesConsequence(d.merchant, d.item, d.sales))
                println("router: sales ", d.sales, " -> sales")
            end
        end
    end

    # consumers: own the ONLY mutable state, each confined to one task.  When the
    # input channel closes, each returns an immutable final result (a sorted
    # tuple of key => value pairs); the mutable Dict is discarded with the task.
    inv_task = @async begin
        inv = Dict{Tuple{Symbol,Symbol},Int}()
        for c in stock_ch
            k = (c.merchant, c.item)
            inv[k] = get(inv, k, 0) + c.delta
            println("inventory: ", c.merchant, " ", c.item, " = ", inv[k])
        end
        Tuple(sort(collect(inv); by = first))
    end

    cash_task = @async begin
        cf = Dict{Symbol,Int}()
        for c in cash_ch
            cf[c.merchant] = get(cf, c.merchant, 0) + c.delta_cents
            println("cashflow: ", c.merchant, " = ", dollars(cf[c.merchant]))
        end
        Tuple(sort(collect(cf); by = first))
    end

    sales_task = @async begin
        sa = Dict{Tuple{Symbol,Symbol},Int}()
        for c in sales_ch
            k = (c.merchant, c.item)
            sa[k] = get(sa, k, 0) + c.delta
            println("sales: ", c.merchant, " ", c.item, " = ", sa[k])
        end
        Tuple(sort(collect(sa); by = first))
    end

    # lifecycle: close-driven termination, made exception-resistant with finally
    wait(source)                                       # pool is closed
    try
        foreach(wait, workers)                         # all tx mapped to Deltas
    finally
        close(results)                                 # ... even if a worker died
    end
    try
        wait(router)                                   # all Deltas routed
    finally
        close(stock_ch); close(cash_ch); close(sales_ch)  # ... even if router died
    end
    inv  = fetch(inv_task)                             # immutable final results
    cash = fetch(cash_task)
    sales = fetch(sales_task)

    # snapshot
    println()
    println("=== snapshot ===")
    for p in inv
        println("inventory: ", p.first[1], " ", p.first[2], " = ", p.second)
    end
    for p in cash
        println("cashflow:  ", p.first, " = ", dollars(p.second))
    end
    for p in sales
        println("sales:     ", p.first[1], " ", p.first[2], " = ", p.second)
    end
end

main()
