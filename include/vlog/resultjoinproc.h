#ifndef _RESULTJOINPROC_H
#define _RESULTJOINPROC_H

#include <vlog/segment.h>
#include <vlog/fctable.h>

#include <vector>

#define MAX_MAPPINGS 8

#define TMPT_THRESHOLD  (32*1024*1024)

// Set USE_DUPLICATE_DETECTION to 1 to enable the duplicate detection code.
// Set to 0 for the AAAI-16 version.
#define USE_DUPLICATE_DETECTION 0

#if USE_DUPLICATE_DETECTION
struct MyHash {
    size_t operator() (const std::vector<Term_t> &p) const {
        uint64_t result = 0;
        for (int i = 0; i < p.size(); i++) {
            uint64_t tmp = p[i];
            while (tmp != 0) {
                result = result * 576460752303422839 + (tmp & 0xFFFF);
                tmp = (tmp >> 16);
            }
        }
        return (size_t) result;
    }
};
struct MyEq {
    bool operator() (const std::vector<Term_t> &p, const std::vector<Term_t> &q) const {
        for (int i = 0; i < p.size(); i++) {
            if (p[i] != q[i]) {
                return false;
            }
        }
        return true;
    }
};

typedef google::dense_hash_set<std::vector<Term_t>, MyHash, MyEq> HashSet;
#endif

class ResultJoinProcessor {
protected:
    const uint8_t rowsize;
    Term_t *row;
    const uint8_t nCopyFromFirst;
    const uint8_t nCopyFromSecond;
    std::pair<uint8_t, uint8_t> posFromFirst[MAX_MAPPINGS];
    std::pair<uint8_t, uint8_t> posFromSecond[MAX_MAPPINGS];
    const int nthreads;
#if USE_DUPLICATE_DETECTION
    HashSet *rowsHash;
    size_t rowCount;
#endif

private:
    virtual void processResults(const int blockid, const bool unique, boost::mutex *m) = 0;

public:

    ResultJoinProcessor(const uint8_t rowsize, const uint8_t nCopyFromFirst,
                        const uint8_t nCopyFromSecond, const std::pair<uint8_t, uint8_t> *posFromFirst,
                        const std::pair<uint8_t, uint8_t> *posFromSecond, const int nthreads) :
        rowsize(rowsize), nCopyFromFirst(nCopyFromFirst), nCopyFromSecond(nCopyFromSecond), nthreads(nthreads) {
        for (uint8_t i = 0; i < nCopyFromFirst; ++i) {
            this->posFromFirst[i] = posFromFirst[i];
        }
        for (uint8_t i = 0; i < nCopyFromSecond; ++i) {
            this->posFromSecond[i] = posFromSecond[i];
        }
        row = new Term_t[rowsize];
#if USE_DUPLICATE_DETECTION
        rowCount = 0;
        rowsHash = NULL;
#endif
    }

#if DEBUG
    virtual void checkSizes() const {
    }
#endif

    virtual void processResults(const int blockid, const Term_t *first,
                                FCInternalTableItr* second, const bool unique) = 0;

    virtual void processResults(const int blockid,
                                const std::vector<const std::vector<Term_t> *> &vectors1, size_t i1,
                                const std::vector<const std::vector<Term_t> *> &vectors2, size_t i2,
                                const bool unique) = 0;

    virtual void processResults(std::vector<int> &blockid, Term_t *p, std::vector<bool> &unique, boost::mutex *m) = 0;

    virtual void processResults(const int blockid, FCInternalTableItr *first,
                                FCInternalTableItr* second, const bool unique) = 0;

    void processResults(const int blockid, const bool unique) {
        processResults(blockid, unique, NULL);
    }

    virtual void processResultsAtPos(const int blockid, const uint8_t pos,
                                     const Term_t v, const bool unique) = 0;

    virtual bool isBlockEmpty(const int blockId, const bool unique) const = 0;

    virtual uint32_t getRowsInBlock(const int blockId, const bool unique) const = 0;

    virtual void addColumns(const int blockid,
                            std::vector<std::shared_ptr<Column>> &columns,
                            const bool unique, const bool sorted) = 0;

    virtual void addColumns(const int blockid, FCInternalTableItr *itr,
                            const bool unique, const bool sorted,
                            const bool lastInsert) = 0;

    virtual void addColumn(const int blockid, const uint8_t pos,
                           std::shared_ptr<Column> column,
                           const bool unique, const bool sorted) = 0;

    virtual bool isEmpty() const = 0;

    virtual uint8_t getRowSize() {
        return rowsize;
    }

    Term_t *getRawRow() {
        return row;
    }

    uint8_t getNCopyFromSecond() const {
        return nCopyFromSecond;
    }

    std::pair<uint8_t, uint8_t> *getPosFromSecond() {
        return posFromSecond;
    }

    uint8_t getNCopyFromFirst() const {
        return nCopyFromFirst;
    }

    const std::pair<uint8_t, uint8_t> *getPosFromFirst() const {
        return posFromFirst;
    }

    virtual void consolidate(const bool isFinished) {}

    virtual ~ResultJoinProcessor() {
        delete[] row;
#if USE_DUPLICATE_DETECTION
        if (rowsHash != NULL) {
            delete rowsHash;
        }
#endif
    }
};

#define SIZE_HASHCOUNT 100000
#define MAX_NSEGMENTS 3
class InterTableJoinProcessor: public ResultJoinProcessor {
private:
    uint32_t currentSegmentSize;
    std::shared_ptr<SegmentInserter> *segments;

    std::shared_ptr<const FCInternalTable> table;

    void enlargeArray(const uint32_t blockid) {
        if (blockid >= currentSegmentSize) {
            std::shared_ptr<SegmentInserter> *newsegments =
                new std::shared_ptr<SegmentInserter>[blockid + 1];
            for (uint32_t i = 0; i < currentSegmentSize; ++i) {
                newsegments[i] = segments[i];
            }
            for (int i = currentSegmentSize; i < blockid + 1; ++i)
                newsegments[i] = shared_ptr<SegmentInserter>
                                 (new SegmentInserter(rowsize));
            currentSegmentSize = blockid + 1;
            delete[] segments;
            segments = newsegments;
        }
    }

#if USE_DUPLICATE_DETECTION
    void processResults(const int blockid, const bool unique, boost::mutex *m);
#else
    void processResults(const int blockid, const bool unique, boost::mutex *m) {
        enlargeArray(blockid);
        if (rowsize == 0) {
            BOOST_LOG_TRIVIAL(debug) << "Added empty row!";
        }
        segments[blockid]->addRow(row, rowsize);
    }
#endif

public:

#if DEBUG
    void checkSizes() const {
        for (int i = 0; i < currentSegmentSize; i++) {
            segments[i]->checkSizes();
        }
    }
#endif

    InterTableJoinProcessor(const uint8_t rowsize,
                            std::vector<std::pair<uint8_t, uint8_t>> &posFromFirst,
                            std::vector<std::pair<uint8_t, uint8_t>> &posFromSecond,
                            const int nthreads);

    void processResults(std::vector<int> &blockid, Term_t *p, std::vector<bool> &unique, boost::mutex *m);

    void processResults(const int blockid, const Term_t *first,
                        FCInternalTableItr* second, const bool unique);

    void processResults(const int blockid, FCInternalTableItr *first,
                        FCInternalTableItr* second, const bool unique);

    void processResults(const int blockid,
                        const std::vector<const std::vector<Term_t> *> &vectors1, size_t i1,
                        const std::vector<const std::vector<Term_t> *> &vectors2, size_t i2,
                        const bool unique);

    void processResultsAtPos(const int blockid, const uint8_t pos,
                             const Term_t v, const bool unique);

    void addColumns(const int blockid,
                    std::vector<std::shared_ptr<Column>> &columns,
                    const bool unique, const bool sorted);

    void addColumn(const int blockid, const uint8_t pos,
                   std::shared_ptr<Column> column, const bool unique,
                   const bool sorted);

    void addColumns(const int blockid, FCInternalTableItr *itr,
                    const bool unique, const bool sorted,
                    const bool lastInsert) {
        //Not supported
        throw 10;
    }

    void consolidate(const bool isFinished);

    bool isBlockEmpty(const int blockId, const bool unique) const;

    bool isEmpty() const;

    uint32_t getRowsInBlock(const int blockId, const bool unique) const;

    std::shared_ptr<const FCInternalTable> getTable();

    /*size_t getUnfilteredDerivation() const {
        return table == NULL ? 0 : table->getNRows();
    }

    uint64_t getUniqueDerivation() const {
        return 0;
    }*/

    ~InterTableJoinProcessor();
};

class FinalTableJoinProcessor: public ResultJoinProcessor {
private:
    std::vector<FCBlock> &listDerivations;
    FCTable *t;
    const Literal literal;
    const RuleExecutionDetails *ruleDetails;
    const uint8_t ruleExecOrder;

    const size_t iteration;

    int nbuffers;
    SegmentInserter **utmpt;
    SegmentInserter **tmpt;
    std::shared_ptr<const Segment> *tmptseg;

    bool addToEndTable;
    bool newDerivation;

    void enlargeBuffers(const int newsize);

    void copyRawRow(const Term_t *first, const Term_t* second);

    void copyRawRow(const Term_t *first, FCInternalTableItr* second);

#if USE_DUPLICATE_DETECTION
    void processResults(const int blockid, const bool unique, boost::mutex *m);
#else
    void mergeTmpt(const int blockid, const bool unique, boost::mutex *m);

    void processResults(const int blockid, const bool unique, boost::mutex *m) {
        enlargeBuffers(blockid + 1);
        if (!unique) {
            if (tmpt[blockid] == NULL) {
                tmpt[blockid] = new SegmentInserter(rowsize);
            }

            tmpt[blockid]->addRow(row);
            if (tmpt[blockid]->getNRows() > TMPT_THRESHOLD) {
                mergeTmpt(blockid, unique, m);
            }
        } else {
            if (utmpt[blockid] == NULL) {
                utmpt[blockid] = new SegmentInserter(rowsize);
            }
            utmpt[blockid]->addRow(row);
        }
    }
#endif

public:
    FinalTableJoinProcessor(std::vector<std::pair<uint8_t, uint8_t>> &posFromFirst,
                            std::vector<std::pair<uint8_t, uint8_t>> &posFromSecond,
                            std::vector<FCBlock> &listDerivations, FCTable *t,
                            Literal &head, const RuleExecutionDetails *detailsRule,
                            const uint8_t ruleExecOrder,
                            const size_t iteration,
                            const bool addToEndTable,
                            const int nthreads);

#if DEBUG
    void checkSizes() const {
        for (int i = 0; i < nbuffers; i++) {
            if (tmpt[i] != NULL) {
                tmpt[i]->checkSizes();
            }
            if (tmptseg[i] != NULL) {
                tmptseg[i]->checkSizes();
            }
            if (utmpt[i] != NULL) {
                utmpt[i]->checkSizes();
            }
        }
    }
#endif

    bool shouldAddToEndTable() {
        return addToEndTable;
    }

    bool hasNewDerivation() const {
        return newDerivation;
    }

    Literal getLiteral() const {
        return literal;
    }

    size_t getIteration() const {
        return iteration;
    }

    FCTable *getTable() const {
        return t;
    }

    void addColumns(const int blockid,
                    std::vector<std::shared_ptr<Column>> &columns,
                    const bool unique, const bool sorted);

    void addColumn(const int blockid, const uint8_t pos,
                   std::shared_ptr<Column> column, const bool unique,
                   const bool sorted);

    void addColumns(const int blockid, FCInternalTableItr *itr,
                    const bool unique, const bool sorted,
                    const bool lastInsert);

    bool isEmpty() const;

    void processResults(std::vector<int> &blockid, Term_t *p, std::vector<bool> &unique, boost::mutex *m);

    void processResults(const int blockid, const Term_t *first,
                        FCInternalTableItr* second, const bool unique);

    void processResults(const int blockid,
                        const std::vector<const std::vector<Term_t> *> &vectors1, size_t i1,
                        const std::vector<const std::vector<Term_t> *> &vectors2, size_t i2,
                        const bool unique);

    void processResults(const int blockid, FCInternalTableItr *first,
                        FCInternalTableItr* second, const bool unique);

    void processResultsAtPos(const int blockid, const uint8_t pos,
                             const Term_t v, const bool unique);

    bool containsUnfilteredDerivation() const {
        if (tmpt != NULL) {
            for (int i = 0; i < nbuffers; ++i) {
                if (tmpt[i] != NULL && !tmpt[i]->isEmpty())
                    return true;
            }
        }
        if (tmptseg != NULL) {
            for (int i = 0; i < nbuffers; ++i) {
                if (tmptseg[i] != NULL && !tmptseg[i]->isEmpty())
                    return true;
            }
        }
        return false;
    }

    bool isBlockEmpty(const int blockId, const bool unique) const;

    uint32_t getRowsInBlock(const int blockId, const bool unique) const;

    void consolidate(const bool isFinished) {
        consolidate(isFinished, false);
    }

    void consolidate(const bool isFinished, const bool forceCheck);

    void consolidateSegment(std::shared_ptr<const Segment> seg);

    std::vector<std::shared_ptr<const Segment>> getAllSegments();

    ~FinalTableJoinProcessor();
};

#endif
