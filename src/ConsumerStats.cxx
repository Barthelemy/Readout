#include "Consumer.h"

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>

#include <math.h>
#include <Common/Timer.h>



#include <Monitoring/MonitoringFactory.h>
using namespace AliceO2::Monitoring;


// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x)/sizeof(x[0])

// function to convert a value in bytes to a prefixed number 3+3 digits
// suffix is the "base unit" to add after calculated prefix, e.g. Byte-> kBytes
std::string NumberOfBytesToString(double value, const char*suffix, int base=1024) {
  const char *prefixes[]={"","k","M","G","T","P"};
  int maxPrefixIndex=STATIC_ARRAY_ELEMENT_COUNT(prefixes)-1;
  int prefixIndex=log(value)/log(base);
  if (prefixIndex>maxPrefixIndex) {
    prefixIndex=maxPrefixIndex;
  }
  if (prefixIndex<0) {
    prefixIndex=0;
  }
  double scaledValue=value/pow(base,prefixIndex);
  char bufStr[64];
  if (suffix==nullptr) {
    suffix="";
  }
  snprintf(bufStr,sizeof(bufStr)-1,"%.03lf %s%s",scaledValue,prefixes[prefixIndex],suffix);
  return std::string(bufStr);
}




class ConsumerStats: public Consumer {
  private:
  uint64_t counterBlocks;
  uint64_t counterBytesTotal;
  uint64_t counterBytesHeader;
  uint64_t counterBytesDiff;
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer monitoringUpdateTimer;
  double elapsedTime; // value used for rates computation
  
  int monitoringEnabled;
  int monitoringUpdatePeriod;
  std::unique_ptr<Collector> monitoringCollector;

  void publishStats() {
    if (monitoringEnabled) {
      // todo: support for long long types
      // https://alice.its.cern.ch/jira/browse/FLPPROT-69

      monitoringCollector->send(counterBlocks, "readout.Blocks");
      monitoringCollector->send(counterBytesTotal, "readout.BytesTotal");
      monitoringCollector->send(counterBytesDiff, "readout.BytesInterval");
//      monitoringCollector->send((counterBytesTotal/(1024*1024)), "readout.MegaBytesTotal");

      counterBytesDiff=0;
    }
  }


  public:
  ConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {

    cfg.getOptionalValue(cfgEntryPoint + ".monitoringEnabled", monitoringEnabled, 0);
    if (monitoringEnabled) {
      cfg.getOptionalValue(cfgEntryPoint + ".monitoringUpdatePeriod", monitoringUpdatePeriod, 10);
      const std::string configFile=cfg.getValue<std::string>(cfgEntryPoint + ".monitoringConfig");
      theLog.log("Monitoring enabled - period %ds - using configuration %s",monitoringUpdatePeriod,configFile.c_str());

      monitoringCollector=MonitoringFactory::Get("influxdb-udp://localhost:1234"); // TODO: Set URL from Configuration
      monitoringCollector->addDerivedMetric("readout.BytesTotal", DerivedMetricMode::RATE);

      monitoringUpdateTimer.reset(monitoringUpdatePeriod*1000000);
    }

    counterBytesTotal=0;
    counterBytesHeader=0;
    counterBlocks=0;
    counterBytesDiff=0;
    runningTime.reset();
    elapsedTime=0.0;

  }
  ~ConsumerStats() {
    if (elapsedTime==0) {
      theLog.log("Stopping stats clock");
      elapsedTime=runningTime.getTime();
    }
    if (counterBytesTotal>0) {
      theLog.log("Stats: %llu blocks, %.2f MB, %.2f%% header overhead",(unsigned long long)counterBlocks,counterBytesTotal/(1024*1024.0),counterBytesHeader*100.0/counterBytesTotal);
      theLog.log("Stats: average block size = %llu bytes",(unsigned long long)counterBytesTotal/counterBlocks);
      theLog.log("Stats: average block rate = %s",NumberOfBytesToString((counterBlocks)/elapsedTime,"Hz",1000).c_str());
      theLog.log("Stats: average throughput = %s",NumberOfBytesToString(counterBytesTotal/elapsedTime,"B/s").c_str());
      theLog.log("Stats: elapsed time = %.5lfs",elapsedTime);
      publishStats();
    } else {
      theLog.log("Stats: no data received");
    }
  }
  int pushData(DataBlockContainerReference &b) {
    
    counterBlocks++;
    int newBytes=b->getData()->header.dataSize;
    counterBytesTotal+=newBytes;
    counterBytesDiff+=newBytes;
    counterBytesHeader+=b->getData()->header.headerSize;

    if (monitoringEnabled) {
      // todo: do not check time every push() if it goes fast...
      if (monitoringUpdateTimer.isTimeout()) {
        publishStats();
        monitoringUpdateTimer.increment();
      }
    }

    return 0;
  }
  
  int starting() {
    theLog.log("Starting stats clock");
    runningTime.reset();
    return 0;
  };

  int stopping() {
    theLog.log("Stopping stats clock");
    elapsedTime=runningTime.getTime();
    return 0;
  };

};




std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerStats>(cfg, cfgEntryPoint);
}
