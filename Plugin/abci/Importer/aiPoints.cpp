#include "pch.h"
#include "aiInternal.h"
#include "aiContext.h"
#include "aiObject.h"
#include "aiSchema.h"
#include "aiPoints.h"
#include "aiMisc.h"
#include "aiMath.h"


template<class T, class U>
inline void Remap(RawVector<T>& dst, const U& src, const RawVector<std::pair<float, int>>& sort_data)
{
    dst.resize_discard(sort_data.size());
    size_t count = std::min<size_t>(sort_data.size(), src->size());
    auto src_data = src->get();
    for (size_t i = 0; i < count; ++i)
        dst[i] = (T)src_data[sort_data[i].second];
}

template<class T, class U>
inline void Assign(RawVector<T>& dst, const U& src, int point_count)
{
    dst.resize_discard(point_count);
    size_t count = std::min<size_t>(point_count, src->size());
    auto src_data = src->get();
    for (size_t i = 0; i < count; ++i)
        dst[i] = (T)src_data[i];
}



aiPointsSample::aiPointsSample(aiPoints *schema)
    : super(schema)
{
}

aiPointsSample::~aiPointsSample()
{
    waitAsync();
}

void aiPointsSample::fillData(aiPointsData &data)
{
    auto body = [this, &data]() {
        data.visibility = visibility;

        if (data.points) {
            if(!m_points_ref.empty())
                m_points_ref.copy_to(data.points);
        }
        if (data.velocities)
        {
            if(!m_velocities.empty())
                m_velocities.copy_to(data.velocities);
            else
                memset(data.velocities, 0, m_points_ref.size() * sizeof(abcV3));
        }
        if (data.ids) {
            if (!m_ids.empty())
                m_ids.copy_to(data.ids);
            else
                memset(data.ids, 0, m_points_ref.size() * sizeof(uint32_t));
        }
        data.center = m_bb_center;
        data.size = m_bb_size;
    };

    if (m_force_sync || !getConfig().async_load)
        body();
    else
        m_async_copy = std::async(std::launch::async, body);
}

void aiPointsSample::getSummary(aiPointsSampleSummary & dst)
{
    dst.count = (int)m_points.size();
}

void aiPointsSample::waitAsync()
{
    if (m_async_copy.valid())
        m_async_copy.wait();
    m_force_sync = false;
}


aiPoints::aiPoints(aiObject *parent, const abcObject &abc)
    : super(parent, abc)
{
    updateSummary();
}

aiPoints::~aiPoints()
{
    waitAsync();
}

void aiPoints::updateSummary()
{
    auto points = m_schema.getPositionsProperty();
    auto velocities = m_schema.getVelocitiesProperty();
    auto ids = m_schema.getIdsProperty();

    m_summary.constant_points = points.isConstant();

    m_summary.has_velocities = velocities.valid();
    if(m_summary.has_velocities)
        m_summary.constant_points = points.isConstant();

    m_summary.has_ids = velocities.valid();
    if (m_summary.has_ids) {
        m_summary.constant_ids = ids.isConstant();
        if (m_summary.constant_ids && getConfig().interpolate_samples && !m_summary.constant_points) {
            m_summary.interpolate_points = true;
            m_summary.has_velocities = true;
            m_summary.compute_velocities = true;
        }
    }
}

const aiPointsSummaryInternal& aiPoints::getSummary() const
{
    return m_summary;
}

aiPoints::Sample* aiPoints::newSample()
{
    return new Sample(this);
}

void aiPoints::readSampleBody(Sample & sample, uint64_t idx)
{
    auto ss = aiIndexToSampleSelector(idx);
    auto ss2 = aiIndexToSampleSelector(idx + 1);
    auto& summary = getSummary();

    readVisibility(sample, ss);

    // points
    m_schema.getPositionsProperty().get(sample.m_points_sp, ss);
    if (summary.interpolate_points) {
        m_schema.getPositionsProperty().get(sample.m_points_sp2, ss2);
    }

    // velocities
    sample.m_velocities_sp.reset();
    auto velocities_prop = m_schema.getVelocitiesProperty();
    if (velocities_prop.valid()) {
        velocities_prop.get(sample.m_velocities_sp, ss);
    }

    // IDs
    sample.m_ids_sp.reset();
    auto ids_prop = m_schema.getIdsProperty();
    if (ids_prop.valid()) {
        ids_prop.get(sample.m_ids_sp, ss);
    }
}

void aiPoints::cookSampleBody(Sample& sample)
{
    auto& summary = getSummary();
    auto& config = getConfig();

    if (!summary.interpolate_points && !m_sample_index_changed)
        return;

    int point_count = (int)sample.m_points_sp->size();
    if (m_sample_index_changed) {
        if (m_sort) {
            sample.m_sort_data.resize(point_count);
            for (int i = 0; i < point_count; ++i) {
                sample.m_sort_data[i].first = ((*sample.m_points_sp)[i] - getSortPosition()).length();
                sample.m_sort_data[i].second = i;
            }

#ifdef _WIN32
            concurrency::parallel_sort
#else
            std::sort
#endif
                (sample.m_sort_data.begin(), sample.m_sort_data.end(),
                    [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });

            Remap(sample.m_points, sample.m_points_sp, sample.m_sort_data);
            if (summary.interpolate_points)
                Remap(sample.m_points2, sample.m_points_sp2, sample.m_sort_data);

            if (!summary.compute_velocities && sample.m_velocities_sp)
                Remap(sample.m_velocities, sample.m_velocities_sp, sample.m_sort_data);

            if (sample.m_ids_sp)
                Remap(sample.m_ids, sample.m_ids_sp, sample.m_sort_data);
        }
        else {
            Assign(sample.m_points, sample.m_points_sp, point_count);
            if (summary.interpolate_points)
                Assign(sample.m_points2, sample.m_points_sp2, point_count);

            if (!summary.compute_velocities && sample.m_velocities_sp)
                Assign(sample.m_velocities, sample.m_velocities_sp, point_count);

            if (sample.m_ids_sp)
                Assign(sample.m_ids, sample.m_ids_sp, point_count);
        }
        sample.m_points_ref = sample.m_points;

        auto& config = getConfig();
        if (config.swap_handedness) {
            SwapHandedness(sample.m_points.data(), (int)sample.m_points.size());
            SwapHandedness(sample.m_points2.data(), (int)sample.m_points2.size());
            SwapHandedness(sample.m_velocities.data(), (int)sample.m_velocities.size());
        }
        if (config.scale_factor != 1.0f) {
            ApplyScale(sample.m_points.data(), (int)sample.m_points.size(), config.scale_factor);
            ApplyScale(sample.m_points2.data(), (int)sample.m_points2.size(), config.scale_factor);
            ApplyScale(sample.m_velocities.data(), (int)sample.m_velocities.size(), config.scale_factor);
        }
        {
            abcV3 bbmin, bbmax;
            MinMax(bbmin, bbmax, sample.m_points.data(), (int)sample.m_points.size());
            sample.m_bb_center = (bbmin + bbmax) * 0.5f;
            sample.m_bb_size = bbmax - bbmin;
        }
    }

    if (summary.interpolate_points) {
        if (summary.compute_velocities)
            sample.m_points_int.swap(sample.m_points_prev);

        sample.m_points_int.resize_discard(sample.m_points.size());
        Lerp(sample.m_points_int.data(), sample.m_points.data(), sample.m_points2.data(),
            (int)sample.m_points.size(), m_current_time_offset);
        sample.m_points_ref = sample.m_points_int;

        if (summary.compute_velocities) {
            sample.m_velocities.resize_discard(sample.m_points.size());
            if (sample.m_points_int.size() == sample.m_points_prev.size()) {
                GenerateVelocities(sample.m_velocities.data(), sample.m_points_int.data(), sample.m_points_prev.data(),
                    (int)sample.m_points_int.size(), config.vertex_motion_scale);
            }
            else {
                sample.m_velocities.zeroclear();
            }
        }
    }
}

void aiPoints::setSort(bool v) { m_sort = v; }
bool aiPoints::getSort() const { return m_sort; }
void aiPoints::setSortPosition(const abcV3& v) { m_sort_position = v; }
const abcV3& aiPoints::getSortPosition() const { return m_sort_position; }
