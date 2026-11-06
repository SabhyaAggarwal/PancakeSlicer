#ifndef DJD_HEURISTIC_HPP
#define DJD_HEURISTIC_HPP

#include <list>
#include <future>
#include <atomic>
#include <functional>

#include "selection_boilerplate.hpp"

namespace libnest2d { namespace selections {





template<class RawShape>
class _DJDHeuristic: public SelectionBoilerplate<RawShape> {
    using Base = SelectionBoilerplate<RawShape>;

    class SpinLock {
        std::atomic_flag& lck_;
    public:

        inline SpinLock(std::atomic_flag& flg): lck_(flg) {}

        inline void lock() {
            while(lck_.test_and_set(std::memory_order_acquire)) {}
        }

        inline void unlock() { lck_.clear(std::memory_order_release); }
    };

public:
    using typename Base::Item;
    using ItemRef = std::reference_wrapper<Item>;




    struct Config {





        bool try_reverse_order = true;





        bool try_pairs = true;






        bool try_triplets = false;










        double initial_fill_proportion = 1.0/3.0;




        double waste_increment = 0.1;







        bool allow_parallel = true;





        bool force_parallel = false;
    };

private:
    using Base::packed_bins_;
    using ItemGroup = typename Base::ItemGroup;

    using Container = ItemGroup;
    Container store_;
    Config config_;

    static const unsigned MAX_ITEMS_SEQUENTIALLY = 30;
    static const unsigned MAX_VERTICES_SEQUENTIALLY = MAX_ITEMS_SEQUENTIALLY*20;

public:

    inline void configure(const Config& config) {
        config_ = config;
    }

    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    void packItems( TIterator first,
                    TIterator last,
                    const TBin& bin,
                    PConfig&& pconfig = PConfig() )
    {
        using Placer = PlacementStrategyLike<TPlacer>;
        using ItemList = std::list<ItemRef>;

        const double bin_area = sl::area(bin);
        const double w = bin_area * config_.waste_increment;

        const double INITIAL_FILL_PROPORTION = config_.initial_fill_proportion;
        const double INITIAL_FILL_AREA = bin_area*INITIAL_FILL_PROPORTION;

        store_.clear();
        store_.reserve(last-first);


        packed_bins_.clear();

        std::copy(first, last, std::back_inserter(store_));

        std::sort(store_.begin(), store_.end(), [](Item& i1, Item& i2) {
            return i1.area() > i2.area();
        });

        size_t glob_vertex_count = 0;
        std::for_each(store_.begin(), store_.end(),
                      [&glob_vertex_count](const Item& item) {
             glob_vertex_count += item.vertexCount();
        });

        std::vector<Placer> placers;

        bool try_reverse = config_.try_reverse_order;


        auto addBin = [this, &placers, &bin, &pconfig]()
        {
            placers.emplace_back(bin);
            packed_bins_.emplace_back();
            placers.back().configure(pconfig);
        };


        using TPair = std::tuple<ItemRef, ItemRef>;
        using TTriplet = std::tuple<ItemRef, ItemRef, ItemRef>;



        auto check_pair = [](const std::vector<TPair>& wrong_pairs,
                ItemRef i1, ItemRef i2)
        {
            return std::any_of(wrong_pairs.begin(), wrong_pairs.end(),
                               [&i1, &i2](const TPair& pair)
            {
                Item& pi1 = std::get<0>(pair), &pi2 = std::get<1>(pair);
                Item& ri1 = i1, &ri2 = i2;
                return (&pi1 == &ri1 && &pi2 == &ri2) ||
                       (&pi1 == &ri2 && &pi2 == &ri1);
            });
        };


        auto check_triplet = [](
                const std::vector<TTriplet>& wrong_triplets,
                ItemRef i1,
                ItemRef i2,
                ItemRef i3)
        {
            return std::any_of(wrong_triplets.begin(),
                               wrong_triplets.end(),
                               [&i1, &i2, &i3](const TTriplet& tripl)
            {
                Item& pi1 = std::get<0>(tripl);
                Item& pi2 = std::get<1>(tripl);
                Item& pi3 = std::get<2>(tripl);
                Item& ri1 = i1, &ri2 = i2, &ri3 = i3;
                return  (&pi1 == &ri1 && &pi2 == &ri2 && &pi3 == &ri3) ||
                        (&pi1 == &ri1 && &pi2 == &ri3 && &pi3 == &ri2) ||
                        (&pi1 == &ri2 && &pi2 == &ri1 && &pi3 == &ri3) ||
                        (&pi1 == &ri3 && &pi2 == &ri2 && &pi3 == &ri1);
            });
        };

        using ItemListIt = typename ItemList::iterator;

        auto largestPiece = [](ItemListIt it, ItemList& not_packed) {
            return it == not_packed.begin()? std::next(it) : not_packed.begin();
        };

        auto secondLargestPiece = [&largestPiece](ItemListIt it,
                ItemList& not_packed) {
            auto ret = std::next(largestPiece(it, not_packed));
            return ret == it? std::next(ret) : ret;
        };

        auto smallestPiece = [](ItemListIt it, ItemList& not_packed) {
            auto last = std::prev(not_packed.end());
            return it == last? std::prev(it) : last;
        };

        auto secondSmallestPiece = [&smallestPiece](ItemListIt it,
                ItemList& not_packed) {
            auto ret = std::prev(smallestPiece(it, not_packed));
            return ret == it? std::prev(ret) : ret;
        };

        auto tryOneByOne =
                [&bin_area]
                (Placer& placer, ItemList& not_packed,
                double waste,
                double& free_area,
                double& filled_area)
        {
            double item_area = 0;
            bool ret = false;
            auto it = not_packed.begin();

            auto pack = [&placer, &not_packed](ItemListIt it) {
                return placer.pack(*it, rem(it, not_packed));
            };

            while(it != not_packed.end() && !ret &&
                  free_area - (item_area = it->get().area()) <= waste)
            {
                if(item_area <= free_area && pack(it) ) {
                    free_area -= item_area;
                    filled_area = bin_area - free_area;
                    ret = true;
                } else
                    it++;
            }

            if(ret) not_packed.erase(it);

            return ret;
        };

        auto tryGroupsOfTwo =
                [&bin_area, &check_pair, &largestPiece, &smallestPiece,
                 try_reverse]
                (Placer& placer, ItemList& not_packed,
                double waste,
                double& free_area,
                double& filled_area)
        {
            double item_area = 0;
            const auto endit = not_packed.end();

            if(not_packed.size() < 2)
                return false;

            double largest_area = not_packed.front().get().area();
            auto itmp = not_packed.begin(); itmp++;
            double second_largest = itmp->get().area();
            if( free_area - second_largest - largest_area > waste)
                return false;



            bool ret = false;
            auto it = not_packed.begin();
            auto it2 = it;

            std::vector<TPair> wrong_pairs;
            using std::placeholders::_1;

            auto trypack = [&placer, &not_packed](ItemListIt it) {
                return placer.trypack(*it, rem(it, not_packed));
            };

            while(it != endit && !ret &&
                  free_area - (item_area = it->get().area()) -
                  largestPiece(it, not_packed)->get().area() <= waste)
            {
                if(item_area + smallestPiece(it, not_packed)->get().area() >
                        free_area ) { it++; continue; }

                auto pr = trypack(it);


                it2 = not_packed.begin();
                double item2_area = 0;
                while(it2 != endit && pr && !ret && free_area -
                      (item2_area = it2->get().area()) - item_area <= waste)
                {
                    double area_sum = item_area + item2_area;

                    if(it == it2 || area_sum > free_area ||
                            check_pair(wrong_pairs, *it, *it2)) {
                        it2++; continue;
                    }

                    placer.accept(pr);
                    auto pr2 = trypack(it2);
                    if(!pr2) {
                        placer.unpackLast();
                        if(try_reverse) {
                            pr2 = trypack(it2);
                            if(pr2) {
                                placer.accept(pr2);
                                auto pr12 = trypack(it);
                                if(pr12) {
                                    placer.accept(pr12);
                                    ret = true;
                                } else {
                                    placer.unpackLast();
                                }
                            }
                        }
                    } else {
                        placer.accept(pr2); ret = true;
                    }

                    if(ret)
                    {
                        free_area -= area_sum;
                        filled_area = bin_area - free_area;
                    } else {
                        wrong_pairs.emplace_back(*it, *it2);
                        it2++;
                    }
                }

                if(!ret) it++;
            }

            if(ret) { not_packed.erase(it); not_packed.erase(it2); }

            return ret;
        };

        auto tryGroupsOfThree =
                [&bin_area,
                 &smallestPiece, &largestPiece,
                 &secondSmallestPiece, &secondLargestPiece,
                 &check_pair, &check_triplet, try_reverse]
                (Placer& placer, ItemList& not_packed,
                double waste,
                double& free_area,
                double& filled_area)
        {
            auto np_size = not_packed.size();
            if(np_size < 3) return false;

            auto it = not_packed.begin();
            const auto endit = not_packed.end();
            auto it2 = it, it3 = it;



            std::vector<TPair> wrong_pairs;
            std::vector<TTriplet> wrong_triplets;

            auto cap = np_size*np_size / 2 ;
            wrong_pairs.reserve(cap);
            wrong_triplets.reserve(cap);


            bool ret = false;

            auto area = [](const ItemListIt& it) {
                return it->get().area();
            };

            auto trypack = [&placer, &not_packed](ItemListIt it) {
                return placer.trypack(*it, rem(it, not_packed));
            };

            auto pack = [&placer, &not_packed](ItemListIt it) {
                return placer.pack(*it, rem(it, not_packed));
            };

            while (it != endit && !ret) {




                Item& largest = *largestPiece(it, not_packed);
                Item& second_largest = *secondLargestPiece(it, not_packed);

                double area_of_two_largest =
                        largest.area() + second_largest.area();



                if(free_area - area(it) - area_of_two_largest > waste)
                    break;


                Item& smallest = *smallestPiece(it, not_packed);
                Item& second_smallest = *secondSmallestPiece(it, not_packed);



                double area_of_two_smallest =
                        smallest.area() + second_smallest.area();

                if(area(it) + area_of_two_smallest > free_area) {
                    it++; continue;
                }

                auto pr = trypack(it);


                if(!pr) { it++; continue; }

                it2 = not_packed.begin();
                double rem2_area = free_area - largest.area();
                double a2_sum = 0;

                while(it2 != endit && !ret &&
                      rem2_area - (a2_sum = area(it) + area(it2)) <= waste) {


                    if(a2_sum != area(it) + area(it2)) throw -1;

                    if(it == it2 || check_pair(wrong_pairs, *it, *it2)) {
                        it2++; continue;
                    }

                    if(a2_sum + smallest.area() > free_area) {
                        it2++; continue;
                    }

                    bool can_pack2 = false;

                    placer.accept(pr);
                    auto pr2 = trypack(it2);
                    auto pr12 = pr;
                    if(!pr2) {
                        placer.unpackLast();
                        if(try_reverse) {
                            pr2 = trypack(it2);
                            if(pr2) {
                                placer.accept(pr2);
                                pr12 = trypack(it);
                                if(pr12) can_pack2 = true;
                                placer.unpackLast();
                            }
                        }
                    } else {
                        placer.unpackLast();
                        can_pack2 = true;
                    }

                    if(!can_pack2) {
                        wrong_pairs.emplace_back(*it, *it2);
                        it2++;
                        continue;
                    }





                    it3 = not_packed.begin();

                    double a3_sum = 0;

                    while(it3 != endit && !ret &&
                          free_area - (a3_sum = a2_sum + area(it3)) <= waste) {


                        if(it3 == it || it3 == it2 ||
                                check_triplet(wrong_triplets, *it, *it2, *it3))
                        { it3++; continue; }

                        if(a3_sum > free_area) { it3++; continue; }

                        placer.accept(pr12); placer.accept(pr2);
                        bool can_pack3 = pack(it3);

                        if(!can_pack3) {
                            placer.unpackLast();
                            placer.unpackLast();
                        }

                        if(!can_pack3 && try_reverse) {

                            std::array<size_t, 3> indices = {0, 1, 2};
                            std::array<typename ItemList::iterator, 3>
                                    candidates = {it, it2, it3};

                            auto tryPack = [&placer, &candidates, &pack](
                                    const decltype(indices)& idx)
                            {
                                std::array<bool, 3> packed = {false};

                                for(auto id : idx) packed.at(id) =
                                        pack(candidates[id]);

                                bool check =
                                std::all_of(packed.begin(),
                                            packed.end(),
                                            [](bool b) { return b; });

                                if(!check) for(bool b : packed) if(b)
                                        placer.unpackLast();

                                return check;
                            };

                            while (!can_pack3 && std::next_permutation(
                                       indices.begin(),
                                       indices.end())){
                                can_pack3 = tryPack(indices);
                            };
                        }

                        if(can_pack3) {

                            free_area -= a3_sum;
                            filled_area = bin_area - free_area;
                            ret = true;
                        } else {
                            wrong_triplets.emplace_back(*it, *it2, *it3);
                            it3++;
                        }

                    }

                    if(!ret) it2++;

                }

                if(!ret) it++;

            }

            if(ret) {
                not_packed.erase(it);
                not_packed.erase(it2);
                not_packed.erase(it3);
            }

            return ret;
        };

        this->template remove_unpackable_items<Placer>(store_, bin, pconfig);

        int acounter = int(store_.size());
        std::atomic_flag flg = ATOMIC_FLAG_INIT;
        SpinLock slock(flg);

        auto makeProgress = [this, &acounter, &slock]
                (Placer& placer, size_t idx, int packednum)
        {

            packed_bins_[idx] = placer.getItems();


            slock.lock();
            acounter -= packednum;
            this->progress_(acounter);
            slock.unlock();
        };

        double items_area = 0;
        for(Item& item : store_) items_area += item.area();


        auto bincount_guess = unsigned(std::ceil(items_area / bin_area));


        bool do_parallel = config_.allow_parallel && bincount_guess > 1 &&
                ((glob_vertex_count >  MAX_VERTICES_SEQUENTIALLY ||
                 store_.size() > MAX_ITEMS_SEQUENTIALLY) ||
                config_.force_parallel);

        if(do_parallel) dout() << "Parallel execution..." << "\n";

        bool do_pairs = config_.try_pairs;
        bool do_triplets = config_.try_triplets;
        StopCondition stopcond = this->stopcond_;


        auto packjob = [INITIAL_FILL_AREA, bin_area, w, do_triplets, do_pairs,
                        stopcond,
                        &tryOneByOne,
                        &tryGroupsOfTwo,
                        &tryGroupsOfThree,
                        &makeProgress]
                        (Placer& placer, ItemList& not_packed, size_t idx)
        {
            double filled_area = placer.filledArea();
            double free_area = bin_area - filled_area;
            double waste = .0;
            bool lasttry = false;

            while(!not_packed.empty() && !stopcond()) {

                {
                    auto it = not_packed.begin();

                    while(it != not_packed.end() && !stopcond() &&
                          filled_area < INITIAL_FILL_AREA)
                    {
                        if(placer.pack(*it, rem(it, not_packed))) {
                            filled_area += it->get().area();
                            free_area = bin_area - filled_area;
                            it = not_packed.erase(it);
                            makeProgress(placer, idx, 1);
                        } else it++;
                    }
                }


                while(tryOneByOne(placer, not_packed, waste, free_area,
                                  filled_area)) {
                    waste = 0; lasttry = false;
                    makeProgress(placer, idx, 1);
                }


                while(do_pairs &&
                      tryGroupsOfTwo(placer, not_packed, waste, free_area,
                                     filled_area)) {
                    waste = 0; lasttry = false;
                    makeProgress(placer, idx, 2);
                }


                while(do_triplets &&
                      tryGroupsOfThree(placer, not_packed, waste, free_area,
                                       filled_area)) {
                    waste = 0; lasttry = false;
                    makeProgress(placer, idx, 3);
                }

                waste += w;
                if(!lasttry && waste > free_area) lasttry = true;
                else if(lasttry) break;
            }
        };

        size_t idx = 0;
        ItemList remaining;

        if(do_parallel) {
            std::vector<ItemList> not_packeds(bincount_guess);


            for(unsigned b = 0; b < bincount_guess; b++) {
                addBin();
                ItemList& not_packed = not_packeds[b];
                for(unsigned idx = b; idx < store_.size(); idx+=bincount_guess) {
                    not_packed.emplace_back(store_[idx]);
                }
            }


            auto job = [&placers, &not_packeds, &packjob](unsigned idx) {
                Placer& placer = placers[idx];
                ItemList& not_packed = not_packeds[idx];
                return packjob(placer, not_packed, idx);
            };


            std::vector<std::future<void>> rets(bincount_guess);

            for(unsigned b = 0; b < bincount_guess; b++) {
                rets[b] = std::async(std::launch::async, job, b);
            }

            for(unsigned fi = 0; fi < rets.size(); ++fi) {
                rets[fi].wait();


                remaining.merge( not_packeds[fi], [](Item& i1, Item& i2) {
                    return i1.area() > i2.area();
                });

            }

            idx = placers.size();


            if(remaining.size() <= placers.size())
            for(size_t j = 0; j < idx && !remaining.empty(); j++) {
                packjob(placers[j], remaining, j);
            }

        } else {
            remaining = ItemList(store_.begin(), store_.end());
        }

        while(!remaining.empty()) {
            addBin();
            packjob(placers[idx], remaining, idx); idx++;
        }

        int binid = 0;
        for(auto &bin : packed_bins_) {
            for(Item& itm : bin) itm.binId(binid);
            binid++;
        }
    }
};

}
}

#endif
